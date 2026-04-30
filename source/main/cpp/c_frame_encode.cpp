#include "ccore/c_target.h"
#include "ccore/c_allocator.h"
#include "ccore/c_memory.h"
#include "ccore/c_qsort.h"

#include "cmui/c_bitstream.h"
#include "cmui/c_srle.h"
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

        static inline u32 s_units_to_bytes(u32 units, u32 bits_per_unit) { return ((units * bits_per_unit) + 7) >> 3; }
        static inline u16 s_rgba888_to_rgb565(u32 rgba) { return ((rgba >> 8) & 0xf800) | ((rgba >> 5) & 0x07e0) | ((rgba >> 3) & 0x001f); }

        static s32 s_compress(nrle::encoder_t& encoder, u8 const* stream, u32 stream_size_in_bits, u8 symbol_bits, u8* out_stream, u8* out_symbol_rb)
        {
            // size in bytes of the encoded stream, or a negative value on error
            const s32 encoded_size = nrle::analyze_bits(&encoder, stream, stream_size_in_bits, symbol_bits);

            nrle::out_t out_stream_info;
            out_stream_info.m_data = out_stream;
            out_stream_info.m_size = (stream_size_in_bits + 7) >> 3;  // We use the uncompressed size as the upper bound for the compressed size

            ASSERT(stream_size_in_bits <= (u32)(encoded_size * 8));  // Compression should not increase the size

            nrle::header_t srle_header;
            const s32      encoded_num_bits = nrle::encode_bits(&encoder, srle_header, out_stream_info);

            ASSERT(encoded_num_bits == encoded_size * 8);  // The encoded size in bits should match the size calculated during analysis

            for (i32 i = 0; i < (1 << symbol_bits); ++i)
                out_symbol_rb[i] = srle_header.m_run_bits[i];

            return encoded_size;
        }

        s32 move_data16(u16 const* stream, u32 count, u16* out_stream)
        {
            // return number of bytes
            return -1;
        }

        s32 encode_frame(encoder_t& encoder, header_t& out_hdr, u8* out_data, u32 out_data_capacity, u32 const* current_img, u32 const* previous_img, u16 width, u16 height, u16 run_length)
        {
            // Initialize header
            out_hdr.m_magic      = 0x4645;  // 'FE' in ASCII
            out_hdr.m_width      = width;
            out_hdr.m_height     = height;
            out_hdr.m_run_length = run_length;

            // Initialize histogram and palette

            g_memory_fill(encoder.m_palette, 0, sizeof(encoder.m_palette));
            g_memory_fill(encoder.m_histogram_count, 0, sizeof(encoder.m_histogram_count));

            for (u32 i = 0; i < 65536; ++i)
                encoder.m_histogram_color[i] = (i16)i;  // Initialize histogram color (index)

            // 1. Build color histogram and palette

            for (u32 y = 0; y < height; ++y)
            {
                //u32 const* previous_img_row    = previous_img + y * width;
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
                u16 const color      = encoder.m_histogram_color[i];
                encoder.m_palette[i] = color;
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
            const u32 p16_stream_size_in_units         = raw_pixel_count;                                   // 16 bits per pixel (raw color)
            const u32 p8_stream_size_in_units          = p8_pixel_count;                                    // 8 bits per pixel
            const u32 p4_stream_size_in_units          = p4_pixel_count;                                    // 4 bits per pixel
            const u32 p2_stream_size_in_units          = p2_pixel_count;                                    // 2 bits per pixel
            const u32 selector_stream_size_in_units    = total_pixel_count;                                 // 2 bits per pixel (SELECTOR_RAW)
            const u32 line_change_stream_size_in_units = height;                                            // 1 bit per line, rounded up to the nearest byte
            const u32 run_change_stream_size_in_units  = ((width + run_length - 1) / run_length) * height;  // 1 bit per run, rounded up to the nearest byte

            // Which one of the compressing streams is the largest, we will use this to introduce a gap into
            // the output buffer here, so that once we start compressing we can make sure we are not overwriting
            // any data that we still need to read.
            u32 max_stream_size_in_bytes = p16_stream_size_in_units * 2;
            if (p8_stream_size_in_units > max_stream_size_in_bytes)
                max_stream_size_in_bytes = p8_stream_size_in_units;
            if (p4_stream_size_in_units * 4 > max_stream_size_in_bytes)
                max_stream_size_in_bytes = p4_stream_size_in_units * 4;
            if (p2_stream_size_in_units * 2 > max_stream_size_in_bytes)
                max_stream_size_in_bytes = p2_stream_size_in_units * 2;
            if (selector_stream_size_in_units * 2 > max_stream_size_in_bytes)
                max_stream_size_in_bytes = selector_stream_size_in_units * 2;
            if (line_change_stream_size_in_units > max_stream_size_in_bytes)
                max_stream_size_in_bytes = line_change_stream_size_in_units;
            if (run_change_stream_size_in_units > max_stream_size_in_bytes)
                max_stream_size_in_bytes = run_change_stream_size_in_units;

            // Setup pointers for each stream, using out_data as a contiguous block of memory for all streams
            u8* p16_stream_ptr = out_data;
            u8* p8_stream_ptr  = p16_stream_ptr + s_units_to_bytes(p16_stream_size_in_units, 16);
            p8_stream_ptr += max_stream_size_in_bytes;
            u8* p4_stream_ptr          = p8_stream_ptr + s_units_to_bytes(p8_stream_size_in_units, 8);
            u8* p2_stream_ptr          = p4_stream_ptr + s_units_to_bytes(p4_stream_size_in_units, 4);
            u8* selector_stream_ptr    = p2_stream_ptr + s_units_to_bytes(p2_stream_size_in_units, 2);
            u8* line_change_stream_ptr = selector_stream_ptr + s_units_to_bytes(selector_stream_size_in_units, 2);
            u8* run_change_stream_ptr  = line_change_stream_ptr + s_units_to_bytes(line_change_stream_size_in_units, 1);

            // Verify output capacity for the worst-case layout.
            u8* const out_data_end = run_change_stream_ptr + s_units_to_bytes(run_change_stream_size_in_units, 1);
            if ((u32)(out_data_end - out_data) > out_data_capacity)
                return -1;

            // Clear all stream buffers so trailing alignment bits are deterministic.
            g_memory_fill(p2_stream_ptr, 0, (u32)(out_data_end - p2_stream_ptr));

            // Initialize header
            init_header(out_hdr, width, height, run_length);

            // Copy palette to header for decoding.
            for (u32 i = 0; i < 276; ++i)
                out_hdr.m_palette[i] = encoder.m_palette[i];

            // Build RGB565 -> palette index map: 0..275 for palette entries, -1 for raw.
            for (u32 i = 0; i < 65536; ++i)
                encoder.m_histogram_color[i] = -1;
            for (u32 i = 0; i < 276; ++i)
            {
                if (encoder.m_histogram_count[encoder.m_palette[i]] == 0)
                    break;
                encoder.m_histogram_color[encoder.m_palette[i]] = (i16)i;
            }

            nbitstream::writer_t p16_writer;
            nbitstream::writer_t p8_writer;
            nbitstream::writer_t p4_writer;
            nbitstream::writer_t p2_writer;
            nbitstream::writer_t selector_writer;
            nbitstream::writer_t line_change_writer;
            nbitstream::writer_t run_change_writer;

            nbitstream::init(&p16_writer, (u8*)p16_stream_ptr, p16_stream_size_in_units * 16);
            nbitstream::init(&p8_writer, p8_stream_ptr, p8_stream_size_in_units * 8);
            nbitstream::init(&p4_writer, p4_stream_ptr, p4_stream_size_in_units * 4);
            nbitstream::init(&p2_writer, p2_stream_ptr, p2_stream_size_in_units * 2);
            nbitstream::init(&selector_writer, selector_stream_ptr, selector_stream_size_in_units * 2);
            nbitstream::init(&line_change_writer, line_change_stream_ptr, line_change_stream_size_in_units * 1);
            nbitstream::init(&run_change_writer, run_change_stream_ptr, run_change_stream_size_in_units * 1);

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

            const u32 p16_units         = nbitstream::finalize(&p16_writer) / 16;
            const u32 p8_units          = nbitstream::finalize(&p8_writer) / 8;
            const u32 p4_units          = nbitstream::finalize(&p4_writer) / 4;
            const u32 p2_units          = nbitstream::finalize(&p2_writer) / 2;
            const u32 selector_units    = nbitstream::finalize(&selector_writer) / 2;
            const u32 line_change_units = nbitstream::finalize(&line_change_writer) / 1;
            const u32 run_change_units  = nbitstream::finalize(&run_change_writer) / 1;

            ASSERT(p16_units == p16_stream_size_in_units);
            ASSERT(p8_units == p8_stream_size_in_units);
            ASSERT(p4_units == p4_stream_size_in_units);
            ASSERT(p2_units == p2_stream_size_in_units);
            ASSERT(selector_units == selector_stream_size_in_units);
            ASSERT(line_change_units == line_change_stream_size_in_units);
            ASSERT(run_change_units == run_change_stream_size_in_units);

            // Some of the stream we are going to apply SRLE to it and this will result in all of the streams either
            // being the same size or smaller.
            // So we will start to re-compute the location of all the streams one by one.
            nrle::encoder_t srle_encoder;

            u8* p16_stream_ptr_srle          = out_data;
            u32 p16_stream_srle_size         = p16_units;  // p16 already exists here and we are not compressing it
            u8* p8_stream_ptr_srle           = p16_stream_ptr_srle + p16_stream_srle_size;
            u32 p8_stream_srle_size          = s_compress(srle_encoder, p8_stream_ptr, p8_units, 8, p8_stream_ptr_srle, out_hdr.m_p8_rb);
            u8* p4_stream_ptr_srle           = p8_stream_ptr_srle + p8_stream_srle_size;
            u32 p4_stream_srle_size          = s_compress(srle_encoder, p4_stream_ptr, p4_units, 4, p4_stream_ptr_srle, out_hdr.m_p4_rb);
            u8* p2_stream_ptr_srle           = p4_stream_ptr_srle + p4_stream_srle_size;
            u32 p2_stream_srle_size          = s_compress(srle_encoder, p2_stream_ptr, p2_units, 2, p2_stream_ptr_srle, out_hdr.m_p2_rb);
            u8* selector_stream_ptr_srle     = p2_stream_ptr_srle + p2_stream_srle_size;
            u32 selector_stream_srle_size    = s_compress(srle_encoder, selector_stream_ptr, selector_units, 2, selector_stream_ptr_srle, out_hdr.m_selector_rb);
            u8* line_change_stream_ptr_srle  = selector_stream_ptr_srle + selector_stream_srle_size;
            u32 line_change_stream_srle_size = s_compress(srle_encoder, line_change_stream_ptr, line_change_units, 2, line_change_stream_ptr_srle, out_hdr.m_line_change_rb);
            u8* run_change_stream_ptr_srle   = line_change_stream_ptr_srle + line_change_stream_srle_size;
            u32 run_change_stream_srle_size  = s_compress(srle_encoder, run_change_stream_ptr, run_change_units, 1, run_change_stream_ptr_srle, out_hdr.m_run_change_rb);

            const u32 encoded_size = p16_stream_srle_size + p8_stream_srle_size + p4_stream_srle_size + p2_stream_srle_size + selector_stream_srle_size + line_change_stream_srle_size + run_change_stream_srle_size;

            // Fill in the header
            out_hdr.m_p16_encoded_size         = p16_stream_srle_size;
            out_hdr.m_p8_encoded_size          = p8_stream_srle_size;
            out_hdr.m_p4_encoded_size          = p4_stream_srle_size;
            out_hdr.m_p2_encoded_size          = p2_stream_srle_size;
            out_hdr.m_selector_encoded_size    = selector_stream_srle_size;
            out_hdr.m_line_change_encoded_size = line_change_stream_srle_size;
            out_hdr.m_run_change_encoded_size  = run_change_stream_srle_size;

            out_hdr.m_p16_stream_decoded_units         = p16_units;
            out_hdr.m_p8_stream_decoded_units          = p8_units;
            out_hdr.m_p4_stream_decoded_units          = p4_units;
            out_hdr.m_p2_stream_decoded_units          = p2_units;
            out_hdr.m_selector_stream_decoded_units    = selector_units;
            out_hdr.m_line_change_stream_decoded_units = line_change_units;
            out_hdr.m_run_change_stream_decoded_units  = run_change_units;

            return encoded_size <= out_data_capacity ? (s32)encoded_size : -1;
        }

    }  // namespace nframe
}  // namespace ncore
