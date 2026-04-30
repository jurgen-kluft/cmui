#ifndef __CMUI_UI_SERVER_H__
#define __CMUI_UI_SERVER_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    class alloc_t;

    namespace nmui
    {
        struct server_t;
        struct client_t;

        enum wait_ready_t
        {
            WAIT_INVALID   = 0,
            WAIT_READY_TX   = 1,
            WAIT_READY_RX   = 2,
            WAIT_READY_TXRX = 3
        };

        // Client thread callback function type, this is called from the client thread to process
        // client messages and interact with the client connection.
        // The client thread is expected to run until the client disconnects or the server signals
        // it to terminate.
        // If the client callback returns a non-zero value, the server will signal the client thread
        // to terminate and close the connection.
        typedef i32 (*client_fn_t)(client_t* client, i32 client_index, void* user_data);

        // server API
        server_t* server_create(alloc_t* allocator, i32 port, i32 max_clients, client_fn_t client_fn, void* user_data);
        void      server_destroy(alloc_t* allocator, server_t* s);

        // start the server, this will create a thread that listens for incoming client connections and spawns a new
        // thread for each client connection that is accepted. Returns 0 on success, -1 on error.
        i32 server_start(server_t* s);

        // stop the server and all running client threads, this will cause all client connections to be closed and all
        // client threads to terminate.
        void server_stop(server_t* s);

        // client API
        // these functions are to be called from the client thread callback (client_fn_t)

        // returns 0 on timeout, 1 if can send, 2 if can recv, 3 if can send and recv, -1 on error
        i32 client_wait(client_t* client, u8 wait_ready, u32 timeout_ms);

        // fills packet with a pointer to the received data and packet_size with the size of
        // the received data, returns 0 on success, -1 on error
        // user is expected to process/copy the received data before calling client_recv again
        // with the same client, as the buffer may be reused for the next received data.
        i32 client_recv(client_t* client, u8*& packet, u32& packet_size);

        // sends the data in packet of size packet_size, returns 0 on success, -1 on error
        i32 client_send(client_t* client, u8* packet, u32 packet_size);

    }  // namespace nmui
}  // namespace ncore

#endif  /// __CMUI_UI_SERVER_H__
