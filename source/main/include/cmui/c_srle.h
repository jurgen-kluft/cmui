#ifndef __CMUI_SELECTIVE_RUN_LENGTH_ENCODING_H__
#define __CMUI_SELECTIVE_RUN_LENGTH_ENCODING_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

#include "cmui/c_bitstream.h"

namespace ncore
{
    namespace nrle
    {
        struct out_t
        {
            u8* data;
            u32 size;
        };

        struct header_t
        {
            u32 decoded_size_in_bits;  // size of the decoded bitstream in bits
            u8  symbol_bits;           // number of bits used to encode each symbol (1, 2, 4 or 8)
            u8  run_bits[];            // per symbol run-bits (size = 2^symbol_bits)
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
        s32 decode_all_bits(const u8* bitstream, out_t& out);

        // initializes a bitstream reader for the encoded bitstream, returns 0 on success or a
        // negative value on error
        struct header_t;
        struct decoder_t
        {
            nbitstream::reader_t m_bitstream;
            header_t*            m_header;
            s32                  m_symbol;  // current symbol being decoded
            u32                  m_rl;      // remaining run length for the current symbol
        };

        s32        decoder_init(decoder_t& decoder, const u8* bitstream);
        inline s32 decode(decoder_t& decoder)
        {
            if (decoder.m_rl  ==  0)
            {
                if (nbitstream::is_end(&decoder.m_bitstream, decoder.m_header->symbol_bits))
                    return -1;  // end of bitstream

                decoder.m_symbol = nbitstream::read_bits(&decoder.m_bitstream, decoder.m_header->symbol_bits);
                const u8 rb = decoder.m_header->run_bits[decoder.m_symbol];
                if (rb > 0)
                {
                    decoder.m_rl = nbitstream::read_bits(&decoder.m_bitstream, rb) + 1;
                }
                else
                {
                    decoder.m_rl = 1;  // raw mode, just one symbol
                }

                --decoder.m_rl;
                return decoder.m_symbol;
            } 

            --decoder.m_rl;
            return decoder.m_symbol;
        }

    }  // namespace nrle
}  // namespace ncore

#endif  // __CMUI_SELECTIVE_RUN_LENGTH_ENCODING_H__
