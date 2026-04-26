#ifndef __CMUI_SELECTIVE_RUN_LENGTH_ENCODING_H__
#define __CMUI_SELECTIVE_RUN_LENGTH_ENCODING_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nrle
    {
        // Selective Run Length Encoding (SRLE) is a simple compression scheme that encodes runs of either zeros or ones,
        // depending on which is more efficient for the given data.
        // The encoder chooses the mode (raw bits, RLE zeros, or RLE ones) based on the input data and the specified run
        // length bits.
        // See `docs/papers/selective_run_length_encoding.pdf` for the paper describing the algorithm and its rationale.

        enum mode_t
        {
            MODE_ZEROS    = 0,
            MODE_ONES     = 1,
            MODE_RAW_BITS = 2,
        };

        struct header_t
        {
            u8 mode;
            u8 run_bits;
        };

        struct out_t
        {
            u8* data;
            u32 size;
        };

        // ---- Encoder / decoder ----
        u32  encode_bits(const u8* bits, u32 bit_count, u32 run_bits, out_t& out, header_t& header);
        void decode_bits(const u8* in_bits, u32 in_bit_cnt, u8* out_bits, u32 out_bit_cnt, const header_t& header);

        // ---- bit reader ----
        struct bit_reader_t
        {
            const u8* data;            // the bitstream data
            u32       bit_pos;         // current position in bits
            u32       run_bits;        // number of bits used to encode the run length in RLE modes
            u32       pending;         // number of pending bits in the current run (if any)
            u32       remaining_bits;  // number of bits remaining in stream
        };

        // ---- RAW reader ----

        void mode_raw_reader_init(bit_reader_t& r, const u8* data, u32 decoded_bit_count);
        s8   mode_raw_read_bit(bit_reader_t& r);

        // ---- RLE_ZEROS reader ----

        void mode_zeros_reader_init(bit_reader_t& r, const u8* data, u32 run_bits, u32 decoded_bit_count);
        s8   mode_zeros_read_bit(bit_reader_t& r);

        // ---- RLE_ONES reader ----

        void mode_ones_reader_init(bit_reader_t& r, const u8* data, u32 run_bits, u32 decoded_bit_count);
        s8   mode_ones_read_bit(bit_reader_t& r);

        // Usage:
        //
        //   s8 b;
        //   while ((b = mode_xxxx_read_bit(reader)) >= 0)
        //   {
        //       process_bit((u8)b);
        //   }


    }  // namespace nrle
}  // namespace ncore

#endif  // __CMUI_SELECTIVE_RUN_LENGTH_ENCODING_H__
