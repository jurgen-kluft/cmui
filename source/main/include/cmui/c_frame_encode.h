#ifndef __CMUI_FRAME_ENCODE_H__
#define __CMUI_FRAME_ENCODE_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nmui
    {
        struct frame_encoder_t
        {
            struct out_t
            {
                u8* data;
                u32 size;
            };

            u32   run_size;
            out_t line_change;
            out_t run_change;
            out_t pixel_mask;
            out_t pixel_data;
        };

    }  // namespace nmui
}  // namespace ncore

#endif  // __CMUI_FRAME_ENCODE_H__
