#ifndef __CMUI_FRAME_DECODE_H__
#define __CMUI_FRAME_DECODE_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nframe
    {
        struct header_t;

        s32 decode_frame(header_t const& header, u8 const* in_data, u32 in_data_size, u16 const* current_img, u16* next_img);

    }  // namespace nmui
}  // namespace ncore

#endif  // __CMUI_FRAME_DECODE_H__
