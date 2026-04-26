#ifndef __CMUI_FRAME_DECODE_H__
#define __CMUI_FRAME_DECODE_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nmui
    {
        struct frame_decoder_t
        {
            struct in_t
            {
                const u8* data;
                u32       size;
                u32       pos;
            };

            u32  run_size;
            in_t line_change;
            in_t run_change;
            in_t pixel_mask;
            in_t pixel_data;
        };

    }  // namespace nmui
}  // namespace ncore

#endif  // __CMUI_FRAME_DECODE_H__
