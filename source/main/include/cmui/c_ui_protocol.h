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
            u8  m_mac[6];         // client MAC address
            u16 m_display_ic;     // display controller id
            u8  m_screen_bpp;     // bits per pixel
            u8  m_color_format;   // e.g., 0 for RGB565, 1 for RGBA8888, etc. (optional, but recommended)
            u16 m_page_id;        // client requests a specific UI page
            u32 m_screen_width;   // screen width in pixels
            u32 m_screen_height;  // screen height in pixels
        };

        // the current span_t binary format is setting up the following constraints:
        // - block size is 16x16
        // - maximum screen resolution is 4096 x 4,096 (8 bits for row and 8 bits for column)

        struct span_t
        {
            u32 m_screen_id;  // the screen this span belongs to, 0 for the main screen, 1..n for additional screens
            u8  m_by;         // 0..255 (row)
            u8  m_bx_start;   // 0..255 (column start)
            u8  m_bx_count;   // 1..255 (column count)
            u8  m_reserved;   // reserved for future use, set to 0
            u32 m_data_len;   // bytes (optional, but recommended)
            // u8  rgb565[];
        };

    }  // namespace nmui
}  // namespace ncore

#endif  /// __CMUI_UI_PROTOCOL_H__
