#include "ccore/c_target.h"
#include "ccore/c_memory.h"

#include "cmui/c_frame_encode.h"

// Frame-to-frame run-based line delta encoder
// Input: RGBA8888 previous + current frame
// Output: separate streams

namespace ncore
{
    namespace nmui
    {
        static inline void write_u8(frame_encoder_t::out_t& s, u8 v) { s.data[s.size++] = v; }
        static inline void write_u16(frame_encoder_t::out_t& s, u16 v)
        {
            s.data[s.size++] = (u8)(v >> 8);
            s.data[s.size++] = (u8)(v & 0xFF);
        }

        static inline u16 rgba8888_to_rgb565(const u8* p) { return (u16)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3)); }

        void encode_frame_delta_runs(const u8* prev_rgba, const u8* cur_rgba, u32 width, u32 height, frame_encoder_t& enc)
        {
            const u32 bpp            = 4;
            const u32 raw_line_bytes = width * 2;

            for (u32 y = 0; y < height; ++y)
            {
                const u8* prev = prev_rgba + y * width * bpp;
                const u8* cur  = cur_rgba + y * width * bpp;

                u32 any_change = 0;
                for (u32 x = 0; x < width; ++x)
                {
                    if (g_memcmp(prev + x * 4, cur + x * 4, 4) != 0)
                    {
                        any_change = 1;
                        break;
                    }
                }

                if (!any_change)
                {
                    write_u8(enc.line_change, 0);
                    continue;
                }
                write_u8(enc.line_change, 1);

                for (u32 rx = 0; rx < width; rx += enc.run_size)
                {
                    u32 end = rx + enc.run_size;
                    if (end > width)
                        end = width;

                    u32 run_change_flag = 0;
                    for (u32 x = rx; x < end; ++x)
                    {
                        if (g_memcmp(prev + x * 4, cur + x * 4, 4) != 0)
                        {
                            run_change_flag = 1;
                            break;
                        }
                    }

                    write_u8(enc.run_change, (u8)run_change_flag);
                    if (!run_change_flag)
                        continue;

                    u32 run_width  = end - rx;
                    u32 mask_words = (run_width + 15) / 16;
                    u16 masks[4]   = {0};
                    u32 changed    = 0;

                    for (u32 i = 0; i < run_width; ++i)
                    {
                        u32 x = rx + i;
                        if (g_memcmp(prev + x * 4, cur + x * 4, 4) != 0)
                        {
                            masks[i / 16] |= (1u << (15 - (i % 16)));
                            changed++;
                        }
                    }

                    u32 delta_bytes = mask_words * 2 + changed * 2;
                    u32 raw_bytes   = run_width * 2;

                    if (delta_bytes >= raw_bytes)
                    {
                        for (u32 i = 0; i < mask_words; ++i)
                            write_u16(enc.pixel_mask, 0xFFFF);

                        for (u32 i = 0; i < run_width; ++i)
                            write_u16(enc.pixel_data, rgba8888_to_rgb565(cur + (rx + i) * 4));
                    }
                    else
                    {
                        for (u32 i = 0; i < mask_words; ++i)
                            write_u16(enc.pixel_mask, masks[i]);

                        for (u32 i = 0; i < run_width; ++i)
                        {
                            if (masks[i / 16] & (1u << (15 - (i % 16))))
                                write_u16(enc.pixel_data, rgba8888_to_rgb565(cur + (rx + i) * 4));
                        }
                    }
                }
            }
        }

    }  // namespace nmui
}  // namespace ncore
