#include "ccore/c_target.h"
#include "ccore/c_memory.h"

#include "cmui/c_frame_decode.h"
#include "cmui/c_frame_codec.h"

namespace ncore
{
    namespace nframe
    {
        s32 decode_frame(header_t const& header, u8 const* in_data, u32 in_data_size, u16 const* current_img, u16* next_img)
        {
            // order of stream in 'in_data' is:
            // u16*      p16_stream_ptr         =
            // u8*       p8_stream_ptr          =
            // u8*       p4_stream_ptr          =
            // u8*       p2_stream_ptr          =
            // u8*       selector_stream_ptr    =
            // u8*       line_change_stream_ptr =
            // u8*       run_change_stream_ptr  =

        }

    }  // namespace nmui
}  // namespace ncore
