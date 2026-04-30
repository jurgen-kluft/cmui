#include "ccore/c_target.h"

#include "cmui/c_ui_server.h"
#include "ccore/c_allocator.h"

#ifdef TARGET_MAC

#    include <sys/socket.h>
#    include <sys/types.h>
#    include <sys/event.h>
#    include <sys/select.h>
#    include <netinet/in.h>
#    include <netinet/tcp.h>
#    include <arpa/inet.h>
#    include <unistd.h>
#    include <fcntl.h>
#    include <errno.h>
#    include <string.h>
#    include <time.h>

#    include <pthread.h>
#    include <stdatomic.h>

namespace ncore
{
    namespace nmui
    {
        // UI Server implementation for macOS using kqueue and pthreads
        // - The server listens for incoming TCP connections on a specified port.
        // - Each client connection is handled in a separate thread.
        // - TCP maximum 'message' size is 64KB, but we can than triple buffer
        //   that to allow for some internal buffering and message framing.
        // - Server can 'signal' to the client to terminate, there are no messages
        //   sent to or from the client thread to the server thread, the client is
        //   expected to just connect and wait until the server signals it to
        //   disconnect. Or the client terminates itself due to the TCP connection
        //   being closed by the remote side.

        // The state machine for the local-client here and the remote-client is:
        // - remote-client connects to server, server accepts and creates local-client
        //   slot, local-client thread starts and waits for messages from remote-client.
        // - local-client can receive messages from remote-client:
        //   - if the message is a client_info_t, the local-client stores the client info
        //     and calls the client_fn callback with the client info.
        //   - if the message is a frame request, the local-client calls the client_fn callback
        //     with the frame request. And the callback will render the frame and send it to the
        //     remote-client. The state of the remote client is expected to be such that it is
        //     waiting for the frame to be rendered and sent back, so that it can display it.
        //     The remote-client is expected to not send any new messages until it receives the
        //     rendered frame, at which point it can send a new frame request.
        // - remote-client disconnects, local-client thread terminates and server slot is freed.

        enum client_state_t
        {
            SLOT_FREE = 0,
            SLOT_CONNECTED,
            SLOT_DISCONNECTED
        };

        struct client_t
        {
            i32         m_fd;
            u32         m_last_seen_ms;
            u8*         m_recv_buffer;
            u32         m_recv_buffer_cap;
            u32         m_recv_size;
            u8          m_thread_started;
            u8          m_reserved0;
            u8          m_peer_valid;
            u8          m_peer_family;
            u8          m_peer_addr[16];
            i32         m_index;
            client_fn_t m_client_fn;
            pthread_t   m_thread;
            atomic_int  m_state;
            server_t*   m_server;
        };

        struct server_t
        {
            alloc_t*    m_allocator;
            void*       m_user_data;
            client_fn_t m_client_fn;
            i32         m_listen_fd;
            i32         m_client_array_cap;
            client_t*   m_client_array;
            pthread_t   m_thread;
            atomic_int  m_running;
            u16         m_port;
            u8          m_thread_started;
        };

        static constexpr u32 CLIENT_RECV_BUFFER_SIZE = 192 * 1024;

        static u32 now_ms()
        {
            timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return (u32)(((u64)ts.tv_sec * 1000ULL) + ((u64)ts.tv_nsec / 1000000ULL));
        }

        static bool set_nonblocking(i32 fd, bool nonblocking)
        {
            i32 flags = fcntl(fd, F_GETFL, 0);
            if (flags < 0)
                return false;
            flags = nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
            return fcntl(fd, F_SETFL, flags) == 0;
        }

        static void close_client_socket(client_t* c)
        {
            if (c->m_fd >= 0)
            {
                shutdown(c->m_fd, SHUT_RDWR);
                close(c->m_fd);
                c->m_fd = -1;
            }
        }

        static i32 allocate_client_slot(server_t* s)
        {
            for (i32 i = 0; i < s->m_client_array_cap; ++i)
            {
                client_t* const c        = &s->m_client_array[i];
                i32             expected = SLOT_FREE;
                if (atomic_compare_exchange_strong(&c->m_state, &expected, SLOT_CONNECTED))
                {
                    c->m_index = i;
                    return i;
                }
            }
            return -1;
        }

        static void extract_peer_key(sockaddr_storage const& peer_addr, u8& out_family, u8 out_addr[16])
        {
            out_family = 0;
            memset(out_addr, 0, 16);

            if (peer_addr.ss_family == AF_INET)
            {
                sockaddr_in const* const v4 = (sockaddr_in const*)&peer_addr;
                out_family                  = 4;
                memcpy(out_addr, &v4->sin_addr.s_addr, 4);
            }
            else if (peer_addr.ss_family == AF_INET6)
            {
                sockaddr_in6 const* const v6 = (sockaddr_in6 const*)&peer_addr;
                out_family                   = 6;
                memcpy(out_addr, v6->sin6_addr.s6_addr, 16);
            }
        }

        static i32 allocate_client_slot_for_peer(server_t* s, sockaddr_storage const& peer_addr)
        {
            u8 peer_family;
            u8 peer_key[16];
            extract_peer_key(peer_addr, peer_family, peer_key);

            if (peer_family != 0)
            {
                for (i32 i = 0; i < s->m_client_array_cap; ++i)
                {
                    client_t* const c = &s->m_client_array[i];
                    if (c->m_peer_valid == 0)
                        continue;
                    if (c->m_peer_family != peer_family)
                        continue;
                    if (memcmp(c->m_peer_addr, peer_key, 16) != 0)
                        continue;

                    i32 expected = SLOT_FREE;
                    if (atomic_compare_exchange_strong(&c->m_state, &expected, SLOT_CONNECTED))
                    {
                        c->m_index = i;
                        return i;
                    }
                }
            }

            i32 const slot = allocate_client_slot(s);
            if (slot >= 0 && peer_family != 0)
            {
                client_t* const c = &s->m_client_array[slot];
                c->m_peer_valid   = 1;
                c->m_peer_family  = peer_family;
                memcpy(c->m_peer_addr, peer_key, 16);
            }
            return slot;
        }

        static void* client_thread_main(void* arg)
        {
            client_t* const c = (client_t*)arg;
            server_t* const s = c->m_server;

            while (atomic_load(&s->m_running) != 0 && atomic_load(&c->m_state) == SLOT_CONNECTED)
            {
                if (c->m_client_fn == nullptr)
                    break;

                i32 const r = c->m_client_fn(c, c->m_index, s->m_user_data);
                if (r != 0)
                    break;
            }

            close_client_socket(c);
            atomic_store(&c->m_state, SLOT_FREE);
            return nullptr;
        }

        static void* server_thread_main(void* arg)
        {
            server_t* const s = (server_t*)arg;

            while (atomic_load(&s->m_running) != 0)
            {
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(s->m_listen_fd, &readfds);

                timeval tv;
                tv.tv_sec  = 0;
                tv.tv_usec = 200 * 1000;

                i32 const sel = select(s->m_listen_fd + 1, &readfds, nullptr, nullptr, &tv);
                if (sel < 0)
                {
                    if (errno == EINTR)
                        continue;
                    break;
                }
                if (sel == 0)
                    continue;

                sockaddr_storage peer_addr;
                socklen_t        peer_len = sizeof(peer_addr);
                i32 const        fd       = accept(s->m_listen_fd, (sockaddr*)&peer_addr, &peer_len);
                if (fd < 0)
                {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                        continue;
                    break;
                }

                i32 const slot = allocate_client_slot_for_peer(s, peer_addr);
                if (slot < 0)
                {
                    close(fd);
                    continue;
                }

                client_t* const c   = &s->m_client_array[slot];
                c->m_fd             = fd;
                c->m_last_seen_ms   = now_ms();
                c->m_recv_size      = 0;
                c->m_server         = s;
                c->m_client_fn      = s->m_client_fn;
                c->m_thread_started = 0;

                int one = 1;
                setsockopt(c->m_fd, IPPROTO_TCP, TCP_NODELAY, &one, (socklen_t)sizeof(one));

                if (pthread_create(&c->m_thread, nullptr, client_thread_main, c) != 0)
                {
                    close_client_socket(c);
                    atomic_store(&c->m_state, SLOT_FREE);
                    continue;
                }

                c->m_thread_started = 1;
            }

            return nullptr;
        }

        server_t* server_create(alloc_t* allocator, i32 port, i32 max_clients, client_fn_t client_fn, void* user_data)
        {
            if (allocator == nullptr || port <= 0 || max_clients <= 0)
                return nullptr;

            server_t* s = g_allocate_and_clear<server_t>(allocator);
            if (s == nullptr)
                return nullptr;

            s->m_allocator        = allocator;
            s->m_user_data        = user_data;
            s->m_client_fn        = client_fn;
            s->m_port             = port;
            s->m_listen_fd        = -1;
            s->m_client_array_cap = max_clients;
            s->m_thread_started   = 0;
            atomic_init(&s->m_running, 0);

            s->m_client_array = g_allocate_array_and_clear<client_t>(allocator, (u32)max_clients);
            if (s->m_client_array == nullptr)
            {
                g_deallocate(allocator, s);
                return nullptr;
            }

            for (i32 i = 0; i < max_clients; ++i)
            {
                client_t* const c    = &s->m_client_array[i];
                c->m_fd              = -1;
                c->m_recv_buffer_cap = CLIENT_RECV_BUFFER_SIZE;
                c->m_recv_buffer     = g_allocate_array_and_clear<u8>(allocator, c->m_recv_buffer_cap);
                c->m_server          = s;
                c->m_index           = i;
                c->m_thread_started  = 0;
                c->m_peer_valid      = 0;
                c->m_peer_family     = 0;
                memset(c->m_peer_addr, 0, sizeof(c->m_peer_addr));
                atomic_init(&c->m_state, SLOT_FREE);

                if (c->m_recv_buffer == nullptr)
                {
                    for (i32 j = 0; j < i; ++j)
                        g_deallocate_array(allocator, s->m_client_array[j].m_recv_buffer);
                    g_deallocate_array(allocator, s->m_client_array);
                    g_deallocate(allocator, s);
                    return nullptr;
                }
            }

            return s;
        }

        void server_destroy(alloc_t* allocator, server_t* s)
        {
            if (s == nullptr)
                return;

            if (allocator == nullptr)
                allocator = s->m_allocator;

            server_stop(s);

            if (s->m_client_array != nullptr)
            {
                for (i32 i = 0; i < s->m_client_array_cap; ++i)
                    g_deallocate_array(allocator, s->m_client_array[i].m_recv_buffer);
                g_deallocate_array(allocator, s->m_client_array);
            }

            g_deallocate(allocator, s);
        }

        i32 server_start(server_t* s)
        {
            if (s == nullptr)
                return -1;

            i32 expected = 0;
            if (!atomic_compare_exchange_strong(&s->m_running, &expected, 1))
                return 0;

            s->m_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (s->m_listen_fd < 0)
            {
                atomic_store(&s->m_running, 0);
                return -1;
            }

            int reuse = 1;
            if (setsockopt(s->m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, (socklen_t)sizeof(reuse)) != 0)
            {
                close(s->m_listen_fd);
                s->m_listen_fd = -1;
                atomic_store(&s->m_running, 0);
                return -1;
            }

            sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family      = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            addr.sin_port        = htons((u16)s->m_port);

            if (bind(s->m_listen_fd, (sockaddr*)&addr, sizeof(addr)) != 0)
            {
                close(s->m_listen_fd);
                s->m_listen_fd = -1;
                atomic_store(&s->m_running, 0);
                return -1;
            }

            if (listen(s->m_listen_fd, s->m_client_array_cap) != 0)
            {
                close(s->m_listen_fd);
                s->m_listen_fd = -1;
                atomic_store(&s->m_running, 0);
                return -1;
            }

            if (!set_nonblocking(s->m_listen_fd, true))
            {
                close(s->m_listen_fd);
                s->m_listen_fd = -1;
                atomic_store(&s->m_running, 0);
                return -1;
            }

            if (pthread_create(&s->m_thread, nullptr, server_thread_main, s) != 0)
            {
                close(s->m_listen_fd);
                s->m_listen_fd = -1;
                atomic_store(&s->m_running, 0);
                return -1;
            }

            s->m_thread_started = 1;
            return 0;
        }

        void server_stop(server_t* s)
        {
            if (s == nullptr)
                return;

            i32 expected = 1;
            if (!atomic_compare_exchange_strong(&s->m_running, &expected, 0))
                return;

            if (s->m_listen_fd >= 0)
            {
                shutdown(s->m_listen_fd, SHUT_RDWR);
                close(s->m_listen_fd);
                s->m_listen_fd = -1;
            }

            if (s->m_thread_started != 0)
            {
                pthread_join(s->m_thread, nullptr);
                s->m_thread_started = 0;
            }

            for (i32 i = 0; i < s->m_client_array_cap; ++i)
            {
                client_t* const c = &s->m_client_array[i];
                if (atomic_load(&c->m_state) == SLOT_CONNECTED)
                {
                    atomic_store(&c->m_state, SLOT_DISCONNECTED);
                    close_client_socket(c);
                }
            }

            for (i32 i = 0; i < s->m_client_array_cap; ++i)
            {
                client_t* const c = &s->m_client_array[i];
                if (c->m_thread_started != 0)
                {
                    pthread_join(c->m_thread, nullptr);
                    c->m_thread_started = 0;
                }
                atomic_store(&c->m_state, SLOT_FREE);
            }
        }

        i32 client_wait(client_t* client, u8 wait_ready, u32 timeout_ms)
        {
            if (client == nullptr || client->m_fd < 0 || wait_ready == WAIT_INVALID)
                return -1;

            fd_set readfds;
            fd_set writefds;
            FD_ZERO(&readfds);
            FD_ZERO(&writefds);

            if ((wait_ready & WAIT_READY_RX) != 0)
                FD_SET(client->m_fd, &readfds);
            if ((wait_ready & WAIT_READY_TX) != 0)
                FD_SET(client->m_fd, &writefds);

            timeval tv;
            tv.tv_sec  = (i32)(timeout_ms / 1000);
            tv.tv_usec = (i32)((timeout_ms % 1000) * 1000);

            i32 const r = select(client->m_fd + 1, &readfds, &writefds, nullptr, &tv);
            if (r < 0)
            {
                if (errno == EINTR)
                    return 0;
                return -1;
            }
            if (r == 0)
                return 0;

            u8 ready = WAIT_INVALID;
            if (FD_ISSET(client->m_fd, &writefds))
                ready = (u8)(ready | WAIT_READY_TX);
            if (FD_ISSET(client->m_fd, &readfds))
                ready = (u8)(ready | WAIT_READY_RX);
            return (i32)ready;
        }

        i32 client_recv(client_t* client, u8*& packet, u32& packet_size)
        {
            packet      = nullptr;
            packet_size = 0;

            if (client == nullptr || client->m_fd < 0 || client->m_recv_buffer == nullptr)
                return -1;

            for (;;)
            {
                ssize_t const n = recv(client->m_fd, client->m_recv_buffer, client->m_recv_buffer_cap, 0);
                if (n > 0)
                {
                    client->m_recv_size    = (u32)n;
                    client->m_last_seen_ms = now_ms();
                    packet                 = client->m_recv_buffer;
                    packet_size            = client->m_recv_size;
                    return 0;
                }
                if (n == 0)
                {
                    atomic_store(&client->m_state, SLOT_DISCONNECTED);
                    return -1;
                }
                if (errno == EINTR)
                    continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    return -1;

                atomic_store(&client->m_state, SLOT_DISCONNECTED);
                return -1;
            }
        }

        i32 client_send(client_t* client, u8* packet, u32 packet_size)
        {
            if (client == nullptr || client->m_fd < 0 || packet == nullptr || packet_size == 0)
                return -1;

            u32 sent = 0;
            while (sent < packet_size)
            {
                ssize_t const n = send(client->m_fd, packet + sent, packet_size - sent, 0);
                if (n > 0)
                {
                    sent += (u32)n;
                    continue;
                }
                if (n < 0 && errno == EINTR)
                    continue;
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                {
                    i32 const wait_r = client_wait(client, WAIT_READY_TX, 50);
                    if (wait_r <= 0)
                        return -1;
                    continue;
                }

                atomic_store(&client->m_state, SLOT_DISCONNECTED);
                return -1;
            }

            client->m_last_seen_ms = now_ms();
            return 0;
        }

    }  // namespace nmui
}  // namespace ncore

#endif  // TARGET_MAC
