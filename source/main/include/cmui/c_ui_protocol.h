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
        enum message_id_t
        {
            MESSAGE_ID_CLIENT_INFO = 0xFEEDD0001,  // client_info_t
            MESSAGE_ID_FRAME_BEGIN = 0xFEEDD0003,  // frame_begin_t
            MESSAGE_ID_FRAME       = 0xFEEDD0004,  // frame_t
            MESSAGE_ID_FRAME_END   = 0xFEEDD0005   // frame_end_t
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
            u32 m_message_id;     // the message id
            u8  m_mac[6];         // client MAC address
            u16 m_display_ic;     // display controller id
            u32 m_color_format;   // e.g., 0 for RGB565, 1 for RGBA8888, etc. (optional, but recommended)
            u32 m_page_id;        // client requests a specific UI page
            u32 m_screen_width;   // screen width in pixels
            u32 m_screen_height;  // screen height in pixels
        };

        struct frame_begin_t
        {
            u32 m_message_id;  // the message id
            u32 m_page_id;     // the page id for which the frame is being rendered
            u32 m_frame_id;    // the frame id, this is an incrementing number for each new frame, starting from 0
        };

        struct frame_t
        {
            u32 m_message_id;  // the message id
            u32 m_page_id;     // the page id for which the frame is being rendered
            u32 m_frame_id;    // the frame id, this should match the frame id sent in the corresponding frame_begin_t
            u32 m_data_size;   // the size of the frame data in bytes
            // followed by m_data_size bytes of frame data (e.g., raw pixel data in the specified color format)
        };

        struct frame_end_t
        {
            u32 m_message_id;  // the message id
            u16 m_page_id;     // the page id for which the frame is being rendered
            u16 m_frame_id;    // the frame id, this should match the frame id sent in the corresponding frame_begin_t and frame_t
            u32 m_result;      // the result of the remote-client processing the frame, e.g., 0 for success, non-zero for error codes (optional)
        };

    }  // namespace nmui
}  // namespace ncore

#endif  /// __CMUI_UI_PROTOCOL_H__
