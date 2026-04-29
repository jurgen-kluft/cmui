#ifndef __CMUI_FRAME_ENCODE_H__
#define __CMUI_FRAME_ENCODE_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nframe
    {
        struct header_t
        {
            u8  m_selector_rb[4];     // SRLE run-bits for each symbol
            u8  m_line_change_rb[2];  // SRLE run-bits for line change stream
            u8  m_run_change_rb[2];   // SRLE run-bits for run change stream
            u8  m_p2_rb[4];           // SRLE run-bits for P2 stream
            u8  m_p4_rb[16];          // SRLE run-bits for P4 stream
            u8  m_p8_rb[256];         // SRLE run-bits for P8 stream
            u16 m_palette[276];       // RGB565 palette (4 colors for P2, 16 colors for P4, 256 colors for P8)
            u16 m_width;              // Width of the image
            u16 m_height;             // Height of the image
            u16 m_run_length;         // Run length for run change stream
        };
        void init_header(header_t& header, u16 width, u16 height, u16 run_length);

        enum selector_e
        {
            SELECTOR_P2  = 0,
            SELECTOR_P4  = 1,
            SELECTOR_P8  = 3,
            SELECTOR_RAW = 2
        };

        struct encoder_t
        {
            u16 m_histogram_color[65536];  // RGB565 histogram, the color or index
            u32 m_histogram_count[65536];  // RGB565 histogram, the frequency of the color in the image
            u16 m_palette[276];            // RGB565 palette
        };

        s32 encode_frame(encoder_t& encoder, u8* out_data, u32 out_data_capacity, u32 const* current_img, u32 const* previous_img, u16 width, u16 height, u16 run_length = 8);
        s32 decode_frame(header_t const& header, u8 const* in_data, u32 in_data_size, u32 const* current_img, u32* next_img);

    }  // namespace nframe
}  // namespace ncore

#endif  // __CMUI_FRAME_ENCODE_H__
