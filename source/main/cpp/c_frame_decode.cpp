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
            // pseudo code for frame decoder
            // for y < height:
            //    line_change = read_symbol(&line_change_reader);
            //    if (line_change == 1)
            //       num_runs = (width + run-length - 1) / run-length
            //       for run in num_runs:
            //          x_begin = run * run-length
            //          x_end   = min(x_begin + run-length, width)
            //          run_change = read_symbol(&run_change_reader);
            //          if (run_change == 1)
            //             run_length = x_end - x_begin;
            //             for r in run_length:
            //                 selector = read_symbol(&selector_reader);
            //                 switch selector:
            //                    case P2: color = palette[read_symbol(&p2_reader)]; break;
            //                    case P4: color = palette[4 + read_symbol(&p4_reader)]; break;
            //                    case P8: color = palette[20 + read_symbol(&p8_reader)]; break;
            //                    case P16: color = *p16_reader++; break;
            //                 write color to current frame at (x_begin + r, y)
            //          else
            //             copy current run[x_begin,x_end) of previous frame to run[x_begin,x_end) of current frame
            //    else
            //       copy the current line from the previous frame to the current frame

            return -1;
        }

    }  // namespace nframe
}  // namespace ncore
