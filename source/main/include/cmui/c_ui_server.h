#ifndef __CMUI_UI_SERVER_H__
#define __CMUI_UI_SERVER_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nmui
    {
#define MAX_CLIENTS      64
#define MSG_QUEUE_SIZE   64
#define MESSAGE_MAX_SIZE (256 * 1024)

        enum msg_type_t
        {
            MSG_NOP = 0,

            /* main -> server */
            MSG_KICK_CLIENT,
            MSG_SEND_TO_CLIENT,

            /* server -> main */
            MSG_CLIENT_CONNECTED,
            MSG_CLIENT_DISCONNECTED
        };

        struct message_t
        {
            u16 msg_type;
            u8  mac[6];
            u32 length;
            u8  data[];
        };

        typedef message_t *(*msg_alloc_fn)(u32 length);
        typedef void (*msg_free_fn)(message_t *);

        struct server_t;
        i32        server_init(server_t *s, i32 port, msg_alloc_fn alloc, msg_free_fn free);
        i32        server_start(server_t *s);
        void       server_stop(server_t *s);
        i32        server_send(server_t *s, message_t *m);  // main -> server
        message_t *server_recv(server_t *s);                // main <- server

    }  // namespace nmui
}  // namespace ncore

#endif  /// __CMUI_UI_SERVER_H__
