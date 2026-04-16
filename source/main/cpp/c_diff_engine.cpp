#include "ccore/c_target.h"
#include "ccore/c_math.h"
#include "ccore/c_memory.h"

#include "cmui/c_diff_engine.h"

namespace ncore
{
    namespace nmui
    {
        static inline u32 s_min_u32(u32 a, u32 b) { return (a < b) ? a : b; }

#define DIFF_BLOCK_W 16
#define DIFF_BPP     16  // RGB565 format, bits per pixel

#define DIFF_MAX_BLOCKS_X 32
#define DIFF_MAX_BLOCKS_Y 32
#define DIFF_MAX_UPDATES  512

        static inline bool diff_blocks_equal(u32 w, u32 h, const u8* prev_fb, const u8* curr_fb, u8 bx, u8 by)
        {
            const u32 xs = bx * DIFF_BLOCK_W;
            const u32 ys = by * DIFF_BLOCK_H;
            const u32 xe = s_min_u32(xs + DIFF_BLOCK_W, w);
            const u32 ye = s_min_u32(ys + DIFF_BLOCK_H, h);

            u32 row_off = ys * w * DIFF_BPP;
            for (u32 y = ys; y < ye; ++y)
            {
                u32 col_off = xs * DIFF_BPP;
                u32 bytes   = (xe - xs) * DIFF_BPP;
                if (g_memcmp(prev_fb + row_off + col_off, curr_fb + row_off + col_off, bytes) != 0)
                    return false;
                row_off += w * DIFF_BPP;
            }
            return true;
        }

        void diff_block_init(diff_block_t& block, u8* payload_buffer, i32 buffer_size)
        {
            block.ci          = 0;
            block.ri          = 0;
            block.cn          = 0;
            block.padding     = 0;
            block.pixel_w     = 0;
            block.pixel_h     = 0;
            block.payload_len = 0;
            block.payload     = (buffer_size > 0) ? payload_buffer : nullptr;
        }

        void diff_engine_init(diff_engine_t& ctx, u32 width, u32 height, const u8* prev_fb, const u8* curr_fb)
        {
            ctx.width          = width;
            ctx.height         = height;
            ctx.columns        = (width + DIFF_BLOCK_W - 1) / DIFF_BLOCK_W;
            ctx.rows           = (height + DIFF_BLOCK_H - 1) / DIFF_BLOCK_H;
            ctx.prev_fb        = prev_fb;
            ctx.curr_fb        = curr_fb;
            ctx.state          = 0;
            ctx.current_column = 0;
            ctx.current_row    = 0;
        }

        // Computes the next diff span that can occur on the current row, starting from the current column.
        // If a span is found, fills in the block info and returns true, otherwise it will continue on to
        // the next row until all rows have been processed, at which point it will return false and the
        // state will be set to 3 (done).
        bool diff_engine_compute(diff_engine_t& ctx, diff_block_t& block)
        {
            if (ctx.state == 3)
                return false;

            while (ctx.current_row < ctx.rows)
            {
                u8 nc    = 0;                   // count of contiguous dirty blocks in the current row
                block.ci = ctx.current_column;  // initialize column index to 0 for the start of the row
                for (; ctx.current_column < ctx.columns; ++ctx.current_column)
                {
                    if (!diff_blocks_equal(ctx.width, ctx.height, ctx.prev_fb, ctx.curr_fb, ctx.current_column, ctx.current_row))
                    {
                        if (nc == 0)
                        {
                            // start of a new run of dirty blocks
                            block.ci = ctx.current_column;
                        }
                        ++nc;
                    }
                    else if (nc > 0)
                    {
                        break;  // end of a run of dirty blocks, emit the block/span
                    }
                }

                if (nc > 0)
                {
                    // End of a run of dirty blocks at the end of the row, emit the block/span
                    block.ri          = ctx.current_row;
                    block.cn          = nc;
                    block.pixel_w     = s_min_u32((u32)nc * DIFF_BLOCK_W, ctx.width - (u32)block.ci * DIFF_BLOCK_W);
                    block.pixel_h     = s_min_u32(DIFF_BLOCK_H, ctx.height - (u32)block.ri * DIFF_BLOCK_H);
                    block.payload_len = (u32)block.pixel_w * block.pixel_h * DIFF_BPP;

                    // Copy the pixel data for this block from the current framebuffer into the block's payload buffer
                    u32 src_row_off = (u32)(block.ri * DIFF_BLOCK_H) * ctx.width * DIFF_BPP;
                    u32 dst_off     = (u32)block.pixel_w * DIFF_BPP;
                    for (u32 y = 0; y < block.pixel_h; ++y)
                    {
                        const u32 src_col_off = (u32)block.ci * DIFF_BLOCK_W * DIFF_BPP;
                        g_memcpy(block.payload + dst_off, ctx.curr_fb + src_row_off + src_col_off, block.pixel_w * DIFF_BPP);
                        src_row_off += ctx.width * DIFF_BPP;
                        dst_off += block.pixel_w * DIFF_BPP;
                    }

                    ctx.state = 2;  // computing
                    return true;
                }

                // No more dirty blocks in this row, move to the next row
                ctx.current_column = 0;
                ++ctx.current_row;
            }

            ctx.state = 3;  // done
            return false;
        }

    }  // namespace nmui
}  // namespace ncore
