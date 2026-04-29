#include "ccore/c_target.h"
#include "ccore/c_allocator.h"
#include "ccore/c_memory.h"
#include "ccore/c_qsort.h"

#include "cmui/c_bitstream.h"
#include "cmui/c_frame_encode.h"

namespace ncore
{
    namespace nframe
    {
        // 7 output streams:
        // - P2 (2-bit symbols)
        // - P4 (4-bit symbols)
        // - P8 (8-bit symbols)
        // - P16 (16-bit symbols)
        // - Line‑change (1‑bit symbols)
        // - Run‑change (1‑bit symbols)
        // - Selector (2‑bit symbols)

        // 1  Build color histogram from RGBA8888 image, as a RGB565 palette counting the colors, then sort by frequency
        //    With the information in the histogram we are able to determine the sizes of the following streams:
        //    - P2 stream
        //    - P4 stream
        //    - P8 stream
        //    - P16 stream
        //    - Selector stream
        // 2. We now have a palette of up to 276 colors (4 in P2, 16 in P4, 256 in P8) with the most frequent colors
        //    Any color that is not in the palete is encoded as SELECTOR_RAW in the selector stream
        // 3. Run change stream size = (width / run_length) * height bits
        // 4. Line change stream size = (height / 8) bits
        // 5. Compare image to previous image to build all the streams

        void init_header(header_t& header, u16 width, u16 height, u16 run_length)
        {
            for (i32 i = 0; i < 4; ++i)
                header.m_selector_rb[i] = 0;

            for (i32 i = 0; i < 2; ++i)
            {
                header.m_line_change_rb[i] = 0;
                header.m_run_change_rb[i]  = 0;
            }

            for (i32 i = 0; i < 4; ++i)
                header.m_p2_rb[i] = 0;

            for (i32 i = 0; i < 16; ++i)
                header.m_p4_rb[i] = 0;

            for (i32 i = 0; i < 256; ++i)
                header.m_p8_rb[i] = 0;

            for (i32 i = 0; i < 276; ++i)
                header.m_palette[i] = 0;

            // Set width, height, and run length in the header
            header.m_width      = width;
            header.m_height     = height;
            header.m_run_length = run_length;
        }

        static s8 s_histogram_cmp_fn(const void* a, const void* b, const void* user_data)
        {
            u16 const  ai              = *(const u16*)a;
            u16 const  bi              = *(const u16*)b;
            u32 const* histogram_count = (const u32*)user_data;
            u32 const  ac              = histogram_count[ai];
            u32 const  bc              = histogram_count[bi];
            if (ac > bc)
                return -1;  // Sort in descending order
            if (ac < bc)
                return 1;  // Sort in descending order
            return 0;
        }

        static inline u32 s_number_of_bits_to_bytes(u32 bits) { return (bits + 7) >> 3; }
        static inline u16 s_rgba888_to_rgb565(u32 rgba) { return ((rgba >> 8) & 0xf800) | ((rgba >> 5) & 0x07e0) | ((rgba >> 3) & 0x001f); }

        s32 encode_frame(encoder_t& encoder, u8* out_data, u32 out_data_capacity, u32 const* current_img, u32 const* previous_img, u16 width, u16 height, u16 run_length)
        {
            // u8*  m_selector_stream;          //
            // u8*  m_line_change_stream;       //
            // u8*  m_run_change_stream;        //
            // u8*  m_p2_stream;                //
            // u8*  m_p4_stream;                //
            // u8*  m_p8_stream;                //
            // u16* m_p16_stream;               //
            // u32  m_selector_stream_size;     // Size of selector stream in bytes
            // u32  m_line_change_stream_size;  // Size of line change stream in bytes
            // u32  m_run_change_stream_size;   // Size of run change stream in bytes
            // u32  m_p2_stream_size;           // Size of P2 stream in bytes
            // u32  m_p4_stream_size;           // Size of P4 stream in bytes
            // u32  m_p8_stream_size;           // Size of P8 stream in bytes
            // u32  m_p16_stream_size;          // Size of P16 stream in bytes

            // Initialize histogram and palette

            g_memory_fill(encoder.m_palette, 0, sizeof(encoder.m_palette));
            g_memory_fill(encoder.m_histogram_count, 0, sizeof(encoder.m_histogram_count));

            for (u32 i = 0; i < 65536; ++i)
                encoder.m_histogram_color[i] = (i16)i;  // Initialize histogram color (index)

            // 1. Build color histogram and palette

            for (u32 y = 0; y < height; ++y)
            {
                u32 const* previous_img_row    = previous_img + y * width;
                u32 const* current_img_row     = current_img + y * width;
                u32 const* current_img_row_end = current_img_row + width;
                while (current_img_row < current_img_row_end)
                {
                    const u16 crgb565 = s_rgba888_to_rgb565(*current_img_row++);
                    encoder.m_histogram_count[crgb565]++;
                }
            }

            // Sort colors by frequency and build palette and histogram index
            nsort::sort(encoder.m_histogram_color, 65536, s_histogram_cmp_fn, (const void*)encoder.m_histogram_count);

            for (u32 i = 0; i < 276; ++i)
            {
                u16 const color = encoder.m_histogram_color[i];
                encoder.m_palette[i]  = color;
                if (encoder.m_histogram_count[color] == 0)
                    break;  // No more colors in the image
            }

            // For P2, P4 and P8 we can calculate the number of pixels that are used
            const u32 total_pixel_count = width * height;
            u32       p2_pixel_count    = 0;
            u32       p4_pixel_count    = 0;
            u32       p8_pixel_count    = 0;
            for (u32 i = 0; i < 4; ++i)
            {
                u16 const color_index = encoder.m_histogram_color[i];
                u32 const count       = encoder.m_histogram_count[color_index];
                if (count == 0)
                    break;  // No more colors in the image
                p2_pixel_count += count;
            }
            for (u32 i = 4; i < 20; ++i)
            {
                u16 const color_index = encoder.m_histogram_color[i];
                u32 const count       = encoder.m_histogram_count[color_index];
                if (count == 0)
                    break;  // No more colors in the image
                p4_pixel_count += count;
            }
            for (u32 i = 20; i < 276; ++i)
            {
                u16 const color_index = encoder.m_histogram_color[i];
                u32 const count       = encoder.m_histogram_count[color_index];
                if (count == 0)
                    break;  // No more colors in the image
                p8_pixel_count += count;
            }
            const u32 raw_pixel_count = width * height - p2_pixel_count - p4_pixel_count - p8_pixel_count;

            // Now we can calculate the size of each stream:
            const u32 p2_stream_size_in_bits          = p2_pixel_count * 2;                                // 2 bits per pixel
            const u32 p4_stream_size_in_bits          = p4_pixel_count * 4;                                // 4 bits per pixel
            const u32 p8_stream_size_in_bits          = p8_pixel_count * 8;                                // 8 bits per pixel
            const u32 p16_stream_size_in_bits         = raw_pixel_count * 16;                              // 16 bits per pixel (raw color)
            const u32 selector_stream_size_in_bits    = total_pixel_count * 2;                             // 2 bits per pixel (SELECTOR_RAW)
            const u32 line_change_stream_size_in_bits = height;                                            // 1 bit per line, rounded up to the nearest byte
            const u32 run_change_stream_size_in_bits  = ((width + run_length - 1) / run_length) * height;  // 1 bit per run, rounded up to the nearest byte

            // Setup pointers for each stream, using out_data as a contiguous block of memory for all streams
            header_t* header                 = (header_t*)out_data;          // Header is at the start of out_data
            u8*       p2_stream_ptr          = out_data + sizeof(header_t);  // Start after the header
            u8*       p4_stream_ptr          = p2_stream_ptr + s_number_of_bits_to_bytes(p2_stream_size_in_bits);
            u8*       p8_stream_ptr          = p4_stream_ptr + s_number_of_bits_to_bytes(p4_stream_size_in_bits);
            u16*      p16_stream_ptr         = (u16*)(p8_stream_ptr + s_number_of_bits_to_bytes(p8_stream_size_in_bits));
            u8*       selector_stream_ptr    = (u8*)p16_stream_ptr + s_number_of_bits_to_bytes(p16_stream_size_in_bits);
            u8*       line_change_stream_ptr = selector_stream_ptr + s_number_of_bits_to_bytes(selector_stream_size_in_bits);
            u8*       run_change_stream_ptr  = line_change_stream_ptr + s_number_of_bits_to_bytes(line_change_stream_size_in_bits);

            // Verify output capacity for the worst-case layout.
            u8* const out_data_end = run_change_stream_ptr + s_number_of_bits_to_bytes(run_change_stream_size_in_bits);
            if ((u32)(out_data_end - out_data) > out_data_capacity)
                return -1;

            // Clear all stream buffers so trailing alignment bits are deterministic.
            g_memory_fill(p2_stream_ptr, 0, (u32)(out_data_end - p2_stream_ptr));

            // Initialize header
            init_header(*header, width, height, run_length);

            // Copy palette to header for decoding.
            for (u32 i = 0; i < 276; ++i)
                header->m_palette[i] = encoder.m_palette[i];

            // Build RGB565 -> palette index map: 0..275 for palette entries, -1 for raw.
            for (u32 i = 0; i < 65536; ++i)
                encoder.m_histogram_color[i] = -1;
            for (u32 i = 0; i < 276; ++i)
            {
                if (encoder.m_histogram_count[encoder.m_palette[i]] == 0)
                    break;
                encoder.m_histogram_color[encoder.m_palette[i]] = (i16)i;
            }

            nbitstream::writer_t p2_writer;
            nbitstream::writer_t p4_writer;
            nbitstream::writer_t p8_writer;
            nbitstream::writer_t p16_writer;
            nbitstream::writer_t selector_writer;
            nbitstream::writer_t line_change_writer;
            nbitstream::writer_t run_change_writer;

            nbitstream::init(&p2_writer, p2_stream_ptr, p2_stream_size_in_bits);
            nbitstream::init(&p4_writer, p4_stream_ptr, p4_stream_size_in_bits);
            nbitstream::init(&p8_writer, p8_stream_ptr, p8_stream_size_in_bits);
            nbitstream::init(&p16_writer, (u8*)p16_stream_ptr, p16_stream_size_in_bits);
            nbitstream::init(&selector_writer, selector_stream_ptr, selector_stream_size_in_bits);
            nbitstream::init(&line_change_writer, line_change_stream_ptr, line_change_stream_size_in_bits);
            nbitstream::init(&run_change_writer, run_change_stream_ptr, run_change_stream_size_in_bits);

            // Now that we have everything set up, we can start encoding the image by comparing it to the
            // previous image and filling the streams accordingly.

            const u32 run_count_per_line = (width + run_length - 1) / run_length;
            for (u32 y = 0; y < height; ++y)
            {
                u32 const* current_img_row  = current_img + y * width;
                u32 const* previous_img_row = previous_img + y * width;

                bool line_changed = false;

                for (u32 run = 0; run < run_count_per_line; ++run)
                {
                    const u32 x0 = run * run_length;
                    const u32 x1 = (x0 + run_length) < width ? (x0 + run_length) : width;

                    bool run_changed = false;
                    for (u32 x = x0; x < x1 && !run_changed; ++x)
                    {
                        const u16 crgb565 = s_rgba888_to_rgb565(current_img_row[x]);
                        const u16 prgb565 = s_rgba888_to_rgb565(previous_img_row[x]);
                        run_changed       = (crgb565 != prgb565);
                    }
                    nbitstream::write_bits(&run_change_writer, run_changed ? 1u : 0u, 1);
                    line_changed = line_changed || run_changed;
                    if (!run_changed)
                        continue;

                    for (u32 x = x0; x < x1; ++x)
                    {
                        const u16 crgb565 = s_rgba888_to_rgb565(current_img_row[x]);

                        // Note:
                        // Below we are ignoring the return value of write_bits for performance reasons
                        // We know that the stream has enough capacity because we calculated and reserved
                        // the correct amount of memory for each stream at the start of this function,
                        // and we are filling the streams in a way that matches the calculated sizes.

                        const u16 palette_index = encoder.m_histogram_color[crgb565];
                        if (palette_index < 4)
                        {
                            nbitstream::write_bits(&selector_writer, SELECTOR_P2, 2);
                            nbitstream::write_bits(&p2_writer, (u32)palette_index, 2);
                        }
                        else if (palette_index < 20)
                        {
                            nbitstream::write_bits(&selector_writer, SELECTOR_P4, 2);
                            nbitstream::write_bits(&p4_writer, (u32)(palette_index - 4), 4);
                        }
                        else if (palette_index < 276)
                        {
                            nbitstream::write_bits(&selector_writer, SELECTOR_P8, 2);
                            nbitstream::write_bits(&p8_writer, (u32)(palette_index - 20), 8);
                        }
                        else
                        {
                            nbitstream::write_bits(&selector_writer, SELECTOR_RAW, 2);
                            nbitstream::write_bits(&p16_writer, (u32)crgb565, 16);
                        }
                    }
                }

                nbitstream::write_bits(&line_change_writer, line_changed ? 1u : 0u, 1);
            }

            const u32 p2_bits          = nbitstream::finalize(&p2_writer);
            const u32 p4_bits          = nbitstream::finalize(&p4_writer);
            const u32 p8_bits          = nbitstream::finalize(&p8_writer);
            const u32 p16_bits         = nbitstream::finalize(&p16_writer);
            const u32 selector_bits    = nbitstream::finalize(&selector_writer);
            const u32 line_change_bits = nbitstream::finalize(&line_change_writer);
            const u32 run_change_bits  = nbitstream::finalize(&run_change_writer);

            ASSERT(p2_bits == p2_stream_size_in_bits);
            ASSERT(p4_bits == p4_stream_size_in_bits);
            ASSERT(p8_bits == p8_stream_size_in_bits);
            ASSERT(p16_bits == p16_stream_size_in_bits);
            ASSERT(selector_bits == selector_stream_size_in_bits);
            ASSERT(line_change_bits == line_change_stream_size_in_bits);
            ASSERT(run_change_bits == run_change_stream_size_in_bits);

            const u32 encoded_size = sizeof(header_t) + s_number_of_bits_to_bytes(p2_bits) + s_number_of_bits_to_bytes(p4_bits) + s_number_of_bits_to_bytes(p8_bits) + s_number_of_bits_to_bytes(p16_bits) + s_number_of_bits_to_bytes(selector_bits) +
                                     s_number_of_bits_to_bytes(line_change_bits) + s_number_of_bits_to_bytes(run_change_bits);

            // Some of the stream we are going to apply SRLE to it

            return encoded_size <= out_data_capacity ? (s32)encoded_size : -1;
        }

    }  // namespace nframe
}  // namespace ncore
