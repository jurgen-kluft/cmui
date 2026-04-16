#ifndef __CMUI_DIFF_ENGINE_H__
#define __CMUI_DIFF_ENGINE_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nmui
    {
        struct diff_block_t
        {
            u8  ci;           // Column index of the block (0-based)
            u8  ri;           // Row index of the block (0-based)
            u8  cn;           // Number of contiguous blocks in the same row
            u8  padding;      // Padding for alignment
            u16 pixel_w;      // Clipped pixel width of the block (can be less than DIFF_BLOCK_W for edge blocks)
            u16 pixel_h;      // Clipped pixel height of the block (can be less than DIFF_BLOCK_H for edge blocks)
            u32 payload_len;  // Length of the pixel data in bytes (pixel_w * pixel_h * DIFF_BPP)
            u8* payload;      // Pointer to the pixel data for this block (in RGB565 format)
        };

#define DIFF_BLOCK_H 16  // Height of block in pixels

        // Initialize a diff_block_t with the provided payload buffer and size.
        // The payload buffer should be large enough to hold the maximum possible
        // block data (image width * DIFF_BLOCK_H * DIFF_BPP).
        void diff_block_init(diff_block_t& block, u8* payload_buffer, i32 buffer_size);

        struct diff_engine_t
        {
            u32       width;           // width of the framebuffer in pixels
            u32       height;          // height of the framebuffer in pixels
            u32       columns;         // columns = number of blocks in the x direction (width / DIFF_BLOCK_W, rounded up)
            u32       rows;            // rows = number of blocks in the y direction (height / DIFF_BLOCK_H, rounded up)
            const u8* prev_fb;         // pointer to previous framebuffer data
            const u8* curr_fb;         // pointer to current framebuffer data
            u32       state;           // 0 = uninitialized, 2 = computing, 3 = done
            u32       current_column;  // current block column being processed
            u32       current_row;     // current block row being processed
        };

        void diff_engine_init(diff_engine_t& ctx, u32 width, u32 height, const u8* prev_fb, const u8* curr_fb);
        bool diff_engine_compute(diff_engine_t& ctx, diff_block_t& block);

    }  // namespace nmui
}  // namespace ncore

#endif  /// __CMUI_DIFF_ENGINE_H__
