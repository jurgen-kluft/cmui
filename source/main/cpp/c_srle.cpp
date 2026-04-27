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

        struct symbol_info_t
        {
            u32 sizeInBitsPerRb[6];  // for each rb, size in bits, encoding with run-bits = (1 << rb)
        };

        s32 encode_bits(const u8* data, u32 data_bits, u8 symbol_bits, out_t& out)
        {
            // output buffer is too small, minimum size is 8 KiB
            if (out.size < 8192)
                return -1;

            // only allowed symbol_bits; 1, 2, 4 or 8
            if (symbol_bits != 1 && symbol_bits != 2 && symbol_bits != 4 && symbol_bits != 8)
                return -1;

            // First figure out the optimal 'run_bits' for each symbol, which determines how the run lengths are encoded in the bitstream.
            // Here we are using the output buffer to store the temporary symbol information
            // (size in bits for each rb) during the analysis phase, and then we will write
            // the header and encoded data to the same buffer after we have determined the
            // optimal run_bits for each symbol.
            symbol_info_t* symbol_info = (symbol_info_t*)out.data;

            // Initialize the symbol info
            const u32 num_symbols      = 1U << symbol_bits;
            const u32 symbol_info_size = sizeof(symbol_info_t) * num_symbols;
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
                for (u32 rb = 0; rb <= 5; ++rb)
                {
                    const u32 ne = (count + (1U << rb) - 1) >> rb;  // number of encoding units needed for this run
                    symbol_info[symbol].sizeInBitsPerRb[rb] += ne * (symbol_bits + rb);
                }
            }

            // Per symbol what is the optimal rb to use
            // Header is at the start of the output buffer, followed by the encoded data.
            header_t* hdr      = (header_t*)out.data;
            const u32 hdr_size = sizeof(header_t) + (sizeof(u8) * (1U << symbol_bits));

            for (u32 symbol = 0; symbol < (1U << symbol_bits); ++symbol)
            {
                u32 best_rb = 0;
                for (u32 rb = 1; rb <= 5; ++rb)
                {
                    if (symbol_info[symbol].sizeInBitsPerRb[rb] < symbol_info[symbol].sizeInBitsPerRb[best_rb])
                        best_rb = rb;
                }
                hdr->run_bits[symbol] = (u8)best_rb;
            }
            hdr->decoded_size_in_bits = data_bits;
            hdr->symbol_bits          = symbol_bits;

            // Now we have the optimal rb for each symbol, we can encode the bitstream accordingly.
            nbitstream::writer_t bitwriter;
            nbitstream::init(&bitwriter, out.data + hdr_size, (out.size - hdr_size) * 8);
            nbitstream::init(&bitreader, data, data_bits);

            num_reads = data_bits / symbol_bits;
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

                const u8 rb = hdr->run_bits[symbol];
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
            return (s32)(hdr_size * 8 + total_bits);
        }

        s32 decoded_size(const u8* bitstream)
        {
            const header_t* hdr = (const header_t*)bitstream;
            return (s32)hdr->decoded_size_in_bits;
        }

        s32 symbol_run_bits(const u8* bitstream, u8 symbol)
        {
            const header_t* hdr = (const header_t*)bitstream;
            if (symbol >= (1U << hdr->symbol_bits))
                return -1;  // invalid symbol
            return (s32)hdr->run_bits[symbol];
        }

        s32 decode_bits(const u8* bitstream, out_t& out)
        {
            const header_t* hdr  = (const header_t*)bitstream;
            const u8*       data = bitstream + sizeof(header_t) + (sizeof(u8) * (1U << hdr->symbol_bits));

            nbitstream::reader_t bitreader;
            nbitstream::init(&bitreader, data, hdr->decoded_size_in_bits);

            nbitstream::writer_t bitwriter;
            nbitstream::init(&bitwriter, out.data, out.size * 8);

            while (nbitstream::is_end(&bitreader, hdr->symbol_bits) == false)
            {
                const u32 symbol = nbitstream::read_bits_unguarded(&bitreader, hdr->symbol_bits);
                if (symbol >= (1U << hdr->symbol_bits))
                    return -1;  // invalid symbol

                const u8 rb = hdr->run_bits[symbol];
                if (rb == 0)
                {
                    // raw mode, just write one symbol
                    if (nbitstream::write_bits(&bitwriter, symbol, hdr->symbol_bits) < 0)
                        return -1;  // error writing bits
                }
                else
                {
                    const u32 chunk = nbitstream::read_bits_unguarded(&bitreader, rb) + 1;
                    for (u32 i = 0; i < chunk; ++i)
                    {
                        if (nbitstream::write_bits(&bitwriter, symbol, hdr->symbol_bits) < 0)
                            return -1;  // error writing bits
                    }
                }
            }

            const u32 total_bits = nbitstream::finalize(&bitwriter);

            return (s32)total_bits;
        }

        s32 decoder_init(decoder_t& decoder, const u8* bitstream)
        {
            const header_t* hdr = (const header_t*)bitstream;
            if (hdr->symbol_bits != 1 && hdr->symbol_bits != 2 && hdr->symbol_bits != 4 && hdr->symbol_bits != 8)
                return -1;  // invalid symbol_bits

            decoder.m_header = (header_t*)bitstream;
            const u32 header_size = sizeof(header_t) + (sizeof(u8) * (1U << hdr->symbol_bits));
            nbitstream::init(&decoder.m_bitstream, bitstream + header_size, (decoder.m_header->decoded_size_in_bits));
            decoder.m_symbol = 0;
            decoder.m_rl     = 0;
            return 0;
        }

    }  // namespace nrle
}  // namespace ncore
