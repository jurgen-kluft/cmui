#include "ccore/c_target.h"
#include "ccore/c_math.h"
#include "ccore/c_memory.h"

#include "cmui/c_srle.h"
#include "cmui/c_bitstream.h"

namespace ncore
{
    namespace nrle
    {
        // bitstream format:
        //  - u32      decoded_size_in_bits;  // size of the decoded bitstream in bits
        //  - u8       symbol_bits;           // number of bits used to encode each symbol (1, 2, 4 or 8)
        //  - u8       run_bits[];            // array of run-bits per symbols (size = 2^symbol_bits)
        //  - u8       encoded_data[];        // the encoded bitstream data

        s32 analyze_bits(encoder_t* enc, const u8* data, u32 data_bits, u8 symbol_bits)
        {
            // only allowed symbol_bits; 1, 2, 4 or 8
            if (symbol_bits != 1 && symbol_bits != 2 && symbol_bits != 4 && symbol_bits != 8)
                return -1;

            // First figure out the optimal 'run_bits' for each symbol, which determines how the run lengths are encoded in the bitstream.
            // Here we are using the output buffer to store the temporary symbol information
            // (size in bits for each rb) during the analysis phase, and then we will write
            // the header and encoded data to the same buffer after we have determined the
            // optimal run_bits for each symbol.
            enc->m_symbol_bits             = symbol_bits;
            encoder_t::info_t* symbol_info = enc->m_symbol_info;

            // Initialize the symbol info
            const u32 num_symbols      = 1U << symbol_bits;
            const u32 symbol_info_size = sizeof(encoder_t::info_t) * num_symbols;
            g_memclr(symbol_info, symbol_info_size);

            nbitstream::reader_t bitreader;
            nbitstream::init(&bitreader, data, data_bits);
            i32 num_reads = data_bits / symbol_bits;
            while (num_reads > 0)
            {
                const u32 symbol = nbitstream::read_bits_unguarded(&bitreader, symbol_bits);
                u32       count  = num_reads;
                --num_reads;
                while (num_reads > 0)
                {
                    const u32 next_symbol = nbitstream::peek_bits_unguarded(&bitreader, symbol_bits);
                    if (next_symbol != symbol)
                        break;
                    nbitstream::skip_bits_unguarded(&bitreader, symbol_bits);
                    --num_reads;
                }
                count = count - num_reads;

                // here for each rb we calculate the size of the encoding and add to
                // the total size for that rb
                encoder_t::info_t& info = symbol_info[symbol];
                info.m_units[0] += count;
                info.m_units[1] += (count + 1) >> 1;
                info.m_units[2] += (count + 3) >> 2;
                info.m_units[3] += (count + 7) >> 3;
                info.m_units[4] += (count + 15) >> 4;
                info.m_units[5] += (count + 31) >> 5;
            }

            // Note:
            // During reading we only have been collecting the number of units per rb,
            // but we need to multiply that with the size of each unit (symbol_bits + rb)
            // to get the total size in bits for each rb.

            u32 final_decoded_size_in_bits = 0;
            for (u32 symbol = 0; symbol < num_symbols; ++symbol)
            {
                u8  best_rb           = 0;
                u32 best_size_in_bits = symbol_info[symbol].m_units[0] * symbol_bits;
                for (u8 rb = 1; rb <= 5; ++rb)
                {
                    const u32 size_in_bits = symbol_info[symbol].m_units[rb] * (symbol_bits + rb);
                    if (size_in_bits < best_size_in_bits)
                    {
                        best_rb           = rb;
                        best_size_in_bits = size_in_bits;
                    }
                }
                enc->m_symbol_rb[symbol] = best_rb;
                final_decoded_size_in_bits += best_size_in_bits;
            }

            enc->m_encoded_size_in_bits = final_decoded_size_in_bits;

            // return the size of the encoded bitstream in bytes (rounded up)
            return (s32)((final_decoded_size_in_bits + 7) >> 3);
        }

        s32 encode_bits(encoder_t const* enc, header_t& out_header, out_t& out_encoded)
        {
            const u32                symbol_bits = enc->m_symbol_bits;
            const encoder_t::info_t* symbol_info = enc->m_symbol_info;

            out_header.m_decoded_size_in_bits = enc->m_data_bits;  // size of the decoded bitstream in bits
            out_header.m_symbol_bits          = symbol_bits;

            // Now we have the optimal rb for each symbol, we can encode the bitstream accordingly.
            nbitstream::reader_t bitreader;
            nbitstream::init(&bitreader, enc->m_data, enc->m_data_bits);

            nbitstream::writer_t bitwriter;
            nbitstream::init(&bitwriter, out_encoded.m_data, out_encoded.m_size * 8);

            u32 num_reads = enc->m_data_bits / symbol_bits;
            while (num_reads > 0)
            {
                const u32 symbol = nbitstream::read_bits_unguarded(&bitreader, symbol_bits);
                u32       count  = num_reads;  // number of sequential occurrences of this symbol
                --num_reads;
                while (num_reads > 0)
                {
                    const u32 next_symbol = nbitstream::peek_bits_unguarded(&bitreader, symbol_bits);
                    if (next_symbol != symbol)
                        break;
                    nbitstream::skip_bits_unguarded(&bitreader, symbol_bits);
                    --num_reads;
                }
                count = count - num_reads;

                const u8 rb = out_header.m_run_bits[symbol];
                if (rb == 0)
                {
                    // raw mode, just write the symbols sequentially without RLE encoding
                    for (u32 i = 0; i < count; ++i)
                    {
                        if (nbitstream::write_bits(&bitwriter, symbol, symbol_bits) < 0)
                            return -1;  // error writing bits
                    }
                }
                else
                {
                    const u32 max_chunk = (1U << rb);
                    u32       remain    = count;
                    while (remain > 0)
                    {
                        const u32 chunk = math::min(remain, max_chunk);
                        if (nbitstream::write_bits(&bitwriter, symbol, symbol_bits) < 0)
                            return -1;  // error writing bits
                        if (nbitstream::write_bits(&bitwriter, chunk - 1, rb) < 0)
                            return -1;  // error writing bits
                        remain -= chunk;
                    }
                }
            }

            const u32 total_bits = nbitstream::finalize(&bitwriter);
            return (s32)(total_bits);
        }

        s32 decoder_init(decoder_t& decoder, const header_t* hdr, const u8* encoded_bitstream)
        {
            if (hdr->m_symbol_bits != 1 && hdr->m_symbol_bits != 2 && hdr->m_symbol_bits != 4 && hdr->m_symbol_bits != 8)
                return -1;  // invalid symbol_bits

            decoder.m_header = (header_t*)hdr;
            decoder.m_symbol = 0;
            decoder.m_rl     = 0;

            nbitstream::init(&decoder.m_bitstream, encoded_bitstream, decoder.m_header->m_decoded_size_in_bits);

            return 0;
        }

        s32 decode_all(decoder_t& decoder, out_t& out)
        {
            const header_t* hdr  = decoder.m_header;

            nbitstream::writer_t bitwriter;
            nbitstream::init(&bitwriter, out.m_data, out.m_size * 8);

            while (nbitstream::is_end(&decoder.m_bitstream, hdr->m_symbol_bits) == false)
            {
                const u32 symbol = nbitstream::read_bits_unguarded(&decoder.m_bitstream, hdr->m_symbol_bits);
                if (symbol >= (1U << hdr->m_symbol_bits))
                    return -1;  // invalid symbol

                const u8 rb = hdr->m_run_bits[symbol];
                if (rb == 0)
                {
                    // raw mode, just write one symbol
                    if (nbitstream::write_bits(&bitwriter, symbol, hdr->m_symbol_bits) < 0)
                        return -1;  // error writing bits
                }
                else
                {
                    const u32 chunk = nbitstream::read_bits_unguarded(&decoder.m_bitstream, rb) + 1;
                    if (nbitstream::write_bits_repeat(&bitwriter, symbol, hdr->m_symbol_bits, chunk) < 0)
                        return -1;  // error writing bits  
                }
            }

            const u32 total_bits = nbitstream::finalize(&bitwriter);
            return (s32)total_bits;
        }

    }  // namespace nrle
}  // namespace ncore
