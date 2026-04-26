#include "ccore/c_target.h"
#include "ccore/c_memory.h"

#include "cmui/c_frame_decode.h"

// Frame-to-frame run-based line delta decoder

namespace ncore
{
    namespace nmui
    {

        static inline u8 read_u8(frame_decoder_t::in_t& s) { return s.data[s.pos++]; }
        static inline u16 read_u16(frame_decoder_t::in_t& s)
        {
            u16 v = (u16)(s.data[s.pos] << 8 | s.data[s.pos + 1]);
            s.pos += 2;
            return v;
        }

        void decode_frame_delta_runs(u16* prev_fb, u16* cur_fb, u32 width, u32 height, frame_decoder_t& dec)
        {
            for (u32 y = 0; y < height; ++y)
            {
                u16* prev = prev_fb + y * width;
                u16* cur  = cur_fb + y * width;

                u8 line_changed = read_u8(dec.line_change);
                if (!line_changed)
                {
                    g_memcpy(cur, prev, width * 2);
                    continue;
                }

                for (u32 rx = 0; rx < width; rx += dec.run_size)
                {
                    u32 end = rx + dec.run_size;
                    if (end > width)
                        end = width;
                    u32 run_width = end - rx;

                    u8 run_changed = read_u8(dec.run_change);
                    if (!run_changed)
                    {
                        g_memcpy(cur + rx, prev + rx, run_width * 2);
                        continue;
                    }

                    u32 mask_words = (run_width + 15) / 16;
                    u16 masks[4];
                    for (u32 i = 0; i < mask_words; ++i)
                        masks[i] = read_u16(dec.pixel_mask);

                    u32 raw_all = 1;
                    for (u32 i = 0; i < mask_words; ++i)
                        if (masks[i] != 0xFFFF)
                            raw_all = 0;

                    if (raw_all)
                    {
                        for (u32 i = 0; i < run_width; ++i)
                            cur[rx + i] = read_u16(dec.pixel_data);
                    }
                    else
                    {
                        for (u32 i = 0; i < run_width; ++i)
                        {
                            u32 w = i / 16;
                            u32 b = 15 - (i % 16);
                            if (masks[w] & (1u << b))
                                cur[rx + i] = read_u16(dec.pixel_data);
                            else
                                cur[rx + i] = prev[rx + i];
                        }
                    }
                }
            }
        }

    }  // namespace nmui
}  // namespace ncore
