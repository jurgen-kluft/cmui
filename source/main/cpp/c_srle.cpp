#include "ccore/c_target.h"
#include "ccore/c_math.h"
#include "ccore/c_memory.h"

#include "cmui/c_srle.h"
#include "cmui/c_bitstream.h"

namespace ncore
{
    namespace nrle
    {
        struct symbol_t
        {
            u8 symbol;    // symbol size can be 1, 2, 4 or 8 bits
            u8 run_bits;  // number of bits used to encode the run length for this symbol (0 for raw mode)
        };

        // bitstream format:
        //  - u32      decoded_size_in_bits;  // size of the decoded bitstream in bits
        //  - u8       symbol_bits;           // number of bits used to encode each symbol (1, 2, 4 or 8)
        //  - u8       run_bits[];            // array of run-bits per symbols (size = 2^symbol_bits)
        //  - u8       encoded_data[];        // the encoded bitstream data

        struct symbol_info_t
        {
            u32 sizeInBitsPerRb[6];  // for each rb, size in bits, encoding with run-bits = (1 << rb)
        };

        struct header_t
        {
            u32 decoded_size_in_bits;  // size of the decoded bitstream in bits
            u8  symbol_bits;           // number of bits used to encode each symbol (1, 2, 4 or 8)
            u8  run_bits[];            // per symbol run-bits (size = 2^symbol_bits)
        };

        s32 encode_bits(const u8* data, u32 data_bits, u8 symbol_bits, out_t& out)
        {
            // First figure out the optimal 'run_bits' for each symbol, which determines how the run lengths are encoded in the bitstream.
            symbol_info_t* symbol_info = (symbol_info_t*)out.data;

            nbitstream::reader_t bitreader;
            nbitstream::init(&bitreader, data, data_bits);
            i32 num_reads = data_bits / symbol_bits;
            while (num_reads > 0)
            {
                const s8 symbol = nbitstream::read_bits_unguarded(&bitreader, symbol_bits);
                u32      run    = num_reads;
                --num_reads;
                while (num_reads > 0)
                {
                    const s8 next_symbol = nbitstream::peek_bits_unguarded(&bitreader, symbol_bits);
                    if (next_symbol != symbol)
                        break;
                    nbitstream::skip_bits_unguarded(&bitreader, symbol_bits);
                    --num_reads;
                }
                run = run - num_reads;

                // here for each rb we calculate the size of the encoding and add to
                // the total size for that rb
                for (u32 rb = 0; rb <= 5; ++rb)
                {
                    u32 r = run;
                    while (r >= (1U << rb))
                    {
                        symbol_info[symbol].sizeInBitsPerRb[rb] += symbol_bits + rb;
                        r -= (1U << rb);
                    }
                }
            }

            // Per symbol what is the optimal rb to use
            header_t* hdr             = (header_t*)out.data;
            hdr->decoded_size_in_bits = data_bits;
            hdr->symbol_bits          = symbol_bits;

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

            const u32 hdr_size = sizeof(header_t) + (sizeof(u8) * (1U << symbol_bits));

            // Now we have the optimal rb for each symbol, we can encode the bitstream accordingly.
            nbitstream::writer_t bitwriter;
            nbitstream::init(&bitwriter, out.data + hdr_size, (out.size - hdr_size) * 8);
            nbitstream::init(&bitreader, data, data_bits);

            num_reads = data_bits / symbol_bits;
            while (num_reads > 0)
            {
                const s8 symbol = nbitstream::read_bits_unguarded(&bitreader, symbol_bits);
                u32      run    = num_reads;
                --num_reads;
                while (num_reads > 0)
                {
                    const s8 next_symbol = nbitstream::peek_bits_unguarded(&bitreader, symbol_bits);
                    if (next_symbol != symbol)
                        break;
                    nbitstream::skip_bits_unguarded(&bitreader, symbol_bits);
                    --num_reads;
                }
                run = run - num_reads;

                const u8 rb = hdr->run_bits[symbol];
                u32 ne = (run + (1U << rb) - 1) >> rb;  // number of encoding units needed for this run
                while (ne > 1)
                {
                    nbitstream::write_bits(&bitwriter, symbol, symbol_bits);
                    nbitstream::write_bits(&bitwriter, (1U << rb) - 1, rb);
                    ne--;
                }
                if (ne > 0)
                {
                    nbitstream::write_bits(&bitwriter, symbol, symbol_bits);
                    nbitstream::write_bits(&bitwriter, (run & ((1U << rb) - 1)) - 1, rb);
                }
            }
        }

    }  // namespace nrle
}  // namespace ncore
