#ifndef __CMUI_UI_PROTOCOL_H__
#define __CMUI_UI_PROTOCOL_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nmui
    {
        enum 
        {
            MSG_ID_CLIENT_INFO = 0x4349,  // 'CI' in ASCII, client_info_t
        };

        // upon connection, the client should send a client_info_t struct to the server, which contains the following:
        // - client MAC address (6 bytes)
        // - display controller id (2 bytes)
        // - bits per pixel (1 byte)
        // - color format (1 byte, e.g., 0 for RGB565, 1 for RGBA8888, etc.)
        // - screen width in pixels (4 bytes)
        // - screen height in pixels (4 bytes)
        // - page id (4 bytes, optional, client can request a specific UI page to be rendered)
        struct client_info_t
        {
            u16 m_message_id;     // the message id
            u16 m_message_len;    // the length of the message, this should be sizeof(client_info_t) for this message
            u8  m_mac[6];         // client MAC address
            u16 m_display_info;   // display information
            u16 m_color_format;   // e.g., 0 for RGB565, 1 for RGBA8888, etc. (optional, but recommended)
            u16 m_page_id;        // client requests a specific UI page
            u16 m_screen_width;   // screen width in pixels
            u16 m_screen_height;  // screen height in pixels
        };

    }  // namespace nmui
}  // namespace ncore

#endif  /// __CMUI_UI_PROTOCOL_H__
