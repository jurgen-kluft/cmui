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
        // ---- Encoder / decoder ----
        // This is a 'selective run-length encoding' (SRLE) algorithm that encodes runs of repeated
        // symbols, and each symbol is assigned 'run bits' used to encode the run length.
        // So before encoding, the input data is analyzed to determine the optimal 'run bits' for
        // each symbol, which are then stored in the header of the encoded bitstream.
        // During decoding, the header is read to reconstruct the symbol information and decode
        // the bitstream accordingly.

        struct out_t
        {
            u8* m_data;
            u32 m_size;
        };

        struct header_t
        {
            u32 m_decoded_size_in_bits;  // size of the decoded bitstream in bits
            u32 m_symbol_bits;           // number of bits used to encode each symbol (1, 2, 4 or 8)
            u8  m_run_bits[256];         // per symbol run-bits (actual size = 2^symbol_bits)
        };

        struct encoder_t
        {
            u8 const* m_data;                  // pointer to the input data to be encoded
            u32       m_data_bits;             // size of the input data in bits (to be encoded)
            u32       m_encoded_size_in_bits;  // size of the encoded bitstream in bits (calculated during analysis)
            u32       m_symbol_bits;           // symbol number of bits (1, 2, 4 or 8))
            struct info_t
            {
                // for each rb, we count the number of units (unit=symbol + run bits) that would be encoded
                // with that rb, which we can then use to calculate the total size in bits for that rb.
                u32 m_units[6];
            };

            info_t m_symbol_info[256];  // max 256 symbols (for symbol_bits = 8)
            u8     m_symbol_rb[256];    // optimal rb for each symbol (for symbol_bits = 8)
        };

        // returns the size of the encoded stream in bytes, or a negative value on error
        // @analysis must is used as temporary storage during the analysis phase to determine
        // the optimal 'run bits' for each symbol and needs to be passed on to encode_bits()
        s32 analyze_bits(encoder_t* enc, const u8* data, u32 data_bits, u8 symbol_bits);

        // returns the number of bits written to the output bitstream, or a negative value on error
        // symbol_bits can be 1, 2, 4 or 8, and determines the size of each symbol in bits
        // note: caller is responsible for ensuring that the output buffer is large enough to hold
        //       the encoded data
        s32 encode_bits(encoder_t const* enc, header_t& out_header, out_t& out_encoded);

        inline s32 decoded_size(const header_t* header) { return (s32)header->m_decoded_size_in_bits; }
        inline s32 symbol_rb(const header_t* hdr, u8 symbol) { return (symbol < (1U << hdr->m_symbol_bits)) ? (s32)hdr->m_run_bits[symbol] : -1; }

        struct decoder_t
        {
            const header_t*      m_header;     // pointer to the header of the encoded bitstream
            nbitstream::reader_t m_bitstream;  // bitstream reader for the encoded data (initialized in decoder_init)
            s32                  m_symbol;     // current symbol being decoded
            u32                  m_rl;         // remaining run length for the current symbol
        };

        // initializes a bitstream reader for the encoded bitstream, returns 0 on success or a
        // negative value on error
        s32 decoder_init(decoder_t& decoder, const header_t* header, const u8* encoded_bitstream);

        // returns the number of bits read from the input bitstream, or a negative value on error
        // note: caller is responsible for ensuring that the output buffer is large enough to hold
        //       the decoded data (see decoded_size() to obtain the size of the decoded bitstream)
        s32 decode_all(decoder_t& decoder, out_t& out);

        // returns the next decoded symbol, or a negative value on error (e.g. end of bitstream)
        inline s32 decode_one(decoder_t& decoder)
        {
            if (decoder.m_rl == 0)
            {
                if (nbitstream::is_end(&decoder.m_bitstream, decoder.m_header->m_symbol_bits))
                    return -1;  // end of bitstream

                decoder.m_symbol = nbitstream::read_bits(&decoder.m_bitstream, decoder.m_header->m_symbol_bits);
                const u8 rb      = decoder.m_header->m_run_bits[decoder.m_symbol];
                if (rb > 0)
                {
                    decoder.m_rl = nbitstream::read_bits(&decoder.m_bitstream, rb) + 1;
                }
                else
                {
                    decoder.m_rl = 1;  // raw mode, just one symbol
                }
            }

            --decoder.m_rl;
            return decoder.m_symbol;
        }

    }  // namespace nrle
}  // namespace ncore

#endif  // __CMUI_SELECTIVE_RUN_LENGTH_ENCODING_H__
