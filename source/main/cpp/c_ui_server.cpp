#include "ccore/c_target.h"

#include "cmui/c_ui_server.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/event.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <pthread.h>
#include <stdatomic.h>

namespace ncore
{
    namespace nmui
    {
        struct msgptr_queue_t
        {
            message_t  *buf[MSG_QUEUE_SIZE];
            atomic_uint head;
            atomic_uint tail;
        };

        enum client_state_t
        {
            SLOT_FREE = 0,
            SLOT_CONNECTED,
            SLOT_DISCONNECTED
        };

        struct client_t
        {
            u8             mac[6];
            i32            fd;
            client_state_t state;
            u32            last_seen_ms;
        };

        struct server_t
        {
            i32 listen_fd;
            i32 kq;

            /* pipe used for wake‑ups (instead of eventfd) */
            i32 wake_pipe_rd;
            i32 wake_pipe_wr;

            client_t       clients[MAX_CLIENTS];
            msgptr_queue_t to_server;
            msgptr_queue_t to_main;

            /* allocator hooks */
            msg_alloc_fn msg_alloc;
            msg_free_fn  msg_free;

            pthread_t  thread;
            atomic_int running;
        };

        static i32 set_nonblocking(i32 fd)
        {
            i32 f = fcntl(fd, F_GETFL, 0);
            return (f < 0) ? -1 : fcntl(fd, F_SETFL, f | O_NONBLOCK);
        }

        static i32 mac_equal(const u8 *a, const u8 *b)
        {
            for (i32 i = 0; i < 6; i++)
                if (a[i] != b[i])
                    return 0;
            return 1;
        }

        static void msgq_init(msgptr_queue_t *q)
        {
            atomic_store(&q->head, 0);
            atomic_store(&q->tail, 0);
            for (unsigned i = 0; i < MSG_QUEUE_SIZE; i++)
                q->buf[i] = NULL;
        }

        static i32 msgq_push(msgptr_queue_t *q, message_t *m)
        {
            unsigned h = atomic_load(&q->head);
            unsigned n = (h + 1) % MSG_QUEUE_SIZE;
            if (n == atomic_load(&q->tail))
                return -1;
            q->buf[h] = m;
            atomic_store(&q->head, n);
            return 0;
        }

        static message_t *msgq_pop(msgptr_queue_t *q)
        {
            unsigned t = atomic_load(&q->tail);
            if (t == atomic_load(&q->head))
                return NULL;
            message_t *m = q->buf[t];
            atomic_store(&q->tail, (t + 1) % MSG_QUEUE_SIZE);
            return m;
        }

        /* ============================================================
         * Client helpers
         * ============================================================ */

        static client_t *find_client_by_mac(server_t *s, const u8 *mac)
        {
            for (i32 i = 0; i < MAX_CLIENTS; i++)
            {
                client_t *c = &s->clients[i];
                if (c->state != SLOT_FREE && mac_equal(c->mac, mac))
                    return c;
            }
            return NULL;
        }

        static client_t *alloc_client(server_t *s)
        {
            for (i32 i = 0; i < MAX_CLIENTS; i++)
                if (s->clients[i].state == SLOT_FREE)
                    return &s->clients[i];
            return NULL;
        }

        /* ============================================================
         * Network setup
         * ============================================================ */

        static i32 create_listen_socket(i32 port)
        {
            i32 fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0)
                return -1;

            i32 yes = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

            struct sockaddr_in addr = {};
            addr.sin_family         = AF_INET;
            addr.sin_addr.s_addr    = htonl(INADDR_ANY);
            addr.sin_port           = htons(port);

            if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
                return -1;
            if (listen(fd, SOMAXCONN) < 0)
                return -1;
            if (set_nonblocking(fd) < 0)
                return -1;

            return fd;
        }

        /* ============================================================
         * HELLO handshake
         * ============================================================ */

        struct hello_msg
        {
            u8 type;
            u8 mac[6];
        } __attribute__((packed));

        static void notify_connected(server_t *s, const u8 mac[6])
        {
            message_t *m = s->msg_alloc(0);
            if (!m)
                return;

            m->type = MSG_CLIENT_CONNECTED;
            m->len  = 0;
            memcpy(m->mac, mac, 6);

            if (msgq_push(&s->to_main, m) < 0)
                s->msg_free(m);
        }

        static void handle_hello(server_t *s, i32 fd)
        {
            struct hello_msg msg;
            ssize_t          n = recv(fd, &msg, sizeof(msg), MSG_PEEK);
            if (n < (ssize_t)sizeof(msg))
                return;

            recv(fd, &msg, sizeof(msg), 0);

            if (msg.type != 0x01)
            {
                close(fd);
                return;
            }

            client_t *c = find_client_by_mac(s, msg.mac);
            if (!c)
            {
                c = alloc_client(s);
                if (!c)
                {
                    close(fd);
                    return;
                }
                memcpy(c->mac, msg.mac, 6);
            }
            else if (c->fd != -1)
            {
                close(c->fd);
            }

            c->fd    = fd;
            c->state = SLOT_CONNECTED;

            struct kevent kev;
            EV_SET(&kev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, c);
            kevent(s->kq, &kev, 1, NULL, 0, NULL);

            notify_connected(s, c->mac);
        }

        /* ============================================================
         * IPC via pipe
         * ============================================================ */

        static void handle_ipc(server_t *s)
        {
            char buf[64];
            read(s->wake_pipe_rd, buf, sizeof(buf));

            for (;;)
            {
                message_t *m = msgq_pop(&s->to_server);
                if (!m)
                    break;

                if (m->type == MSG_KICK_CLIENT)
                {
                    client_t *c = find_client_by_mac(s, m->mac);
                    if (c && c->fd != -1)
                    {
                        close(c->fd);
                        c->fd    = -1;
                        c->state = SLOT_DISCONNECTED;
                    }
                }
                s->msg_free(m);
            }
        }

        /* ============================================================
         * Server thread
         * ============================================================ */

        static void *server_thread_main(void *arg)
        {
            server_t     *s = (server_t *)arg;
            struct kevent events[64];

            while (atomic_load(&s->running))
            {
                i32 n = kevent(s->kq, NULL, 0, events, 64, NULL);
                for (i32 i = 0; i < n; i++)
                {
                    if (events[i].ident == (uintptr_t)s->listen_fd)
                    {
                        for (;;)
                        {
                            i32 fd = accept(s->listen_fd, NULL, NULL);
                            if (fd < 0)
                                break;
                            set_nonblocking(fd);

                            struct kevent kev;
                            EV_SET(&kev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
                            kevent(s->kq, &kev, 1, NULL, 0, NULL);
                        }
                    }
                    else if (events[i].ident == (uintptr_t)s->wake_pipe_rd)
                    {
                        handle_ipc(s);
                    }
                    else
                    {
                        handle_hello(s, (i32)events[i].ident);
                    }
                }
            }
            return NULL;
        }

        /* ============================================================
         * Public API
         * ============================================================ */

        i32 server_init(server_t *s, i32 port, msg_alloc_fn msg_alloc, msg_free_fn msg_free)
        {
            memset(s, 0, sizeof(*s));

            s->msg_alloc = msg_alloc;
            s->msg_free  = msg_free;

            for (i32 i = 0; i < MAX_CLIENTS; i++)
            {
                s->clients[i].fd    = -1;
                s->clients[i].state = SLOT_FREE;
            }

            msgq_init(&s->to_server);
            msgq_init(&s->to_main);

            s->listen_fd = create_listen_socket(port);
            s->kq        = kqueue();

            i32 p[2];
            pipe(p);
            s->wake_pipe_rd = p[0];
            s->wake_pipe_wr = p[1];
            set_nonblocking(s->wake_pipe_rd);

            struct kevent kev;
            EV_SET(&kev, s->listen_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
            kevent(s->kq, &kev, 1, NULL, 0, NULL);

            EV_SET(&kev, s->wake_pipe_rd, EVFILT_READ, EV_ADD, 0, 0, NULL);
            kevent(s->kq, &kev, 1, NULL, 0, NULL);

            return 0;
        }

        i32 server_start(server_t *s)
        {
            atomic_store(&s->running, 1);
            return pthread_create(&s->thread, NULL, server_thread_main, s);
        }

        void server_stop(server_t *s)
        {
            atomic_store(&s->running, 0);
            close(s->listen_fd);
            write(s->wake_pipe_wr, "x", 1);
            pthread_join(s->thread, NULL);
        }

        i32 server_send(server_t *s, message_t *m)
        {
            if (msgq_push(&s->to_server, m) < 0)
                return -1;
            write(s->wake_pipe_wr, "x", 1);
            return 0;
        }

        message_t *server_recv(server_t *s) { return msgq_pop(&s->to_main); }

    }  // namespace nmui
}  // namespace ncore
