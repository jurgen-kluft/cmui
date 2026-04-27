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
        struct out_t
        {
            u8* data;
            u32 size;
        };

        // ---- Encoder / decoder ----
        // This is a 'selective run-length encoding' (SRLE) algorithm that encodes runs of repeated
        // symbols, and each symbol is assigned 'run bits' used to encode the run length.
        // So before encoding, the input data is analyzed to determine the optimal 'run bits' for
        // each symbol, which are then stored in the header of the encoded bitstream.
        // During decoding, the header is read to reconstruct the symbol information and decode
        // the bitstream accordingly.

        // returns the number of bits written to the output bitstream, or a negative value on error
        // symbol_bits can be 1, 2, 4 or 8, and determines the size of each symbol in bits
        // note: caller is responsible for ensuring that the output buffer is large enough to hold
        //       the header block + encoded data 
        // note: minimum size for the output buffer is 8 KiB!
        s32 encode_bits(const u8* data, u32 data_bits, u8 symbol_bits, out_t& out);

        // returns the size of the decoded bitstream in bits, or a negative value on error
        s32 decoded_size(const u8* bitstream);

        // returns the 'run bits' for a given symbol, or a negative value on error
        s32 symbol_run_bits(const u8* bitstream, u8 symbol);

        // returns the number of bits read from the input bitstream, or a negative value on error
        // note: caller is responsible for ensuring that the output buffer is large enough to hold
        //       the decoded data (see decoded_size() to obtain the size of the decoded bitstream)
        s32 decode_bits(const u8* bitstream, out_t& out);

        // ---- reader ----
        // The reader is a utility for reading bits from the encoded bitstream during decoding.
        // It maintains the current position in the bitstream and provides functions for reading
        // bits and symbols.
        struct symbol_t;
        struct reader_t
        {
            const u8*       m_data;            // the bitstream data
            const symbol_t* m_symbols;         // array of symbols (points in bitstream) (size = m_num_symbols)
            u32             m_num_symbols;     // number of symbols (also tells us symbol size in bits)
            u32             m_bit_pos;         // current position in bits
            u32             m_remaining_bits;  // number of bits remaining in stream
            u32             m_reserved;        // reserved for future use, should be set to 0
        };

        void reader_init(reader_t& r, const u8* bitstream);

        // returns the bits read as a signed value, or a negative value on error
        s32 read_bits(reader_t& r, u32 num_bits);

    }  // namespace nrle
}  // namespace ncore

#endif  // __CMUI_SELECTIVE_RUN_LENGTH_ENCODING_H__
