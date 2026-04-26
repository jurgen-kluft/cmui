#include "ccore/c_target.h"
#include "ccore/c_memory.h"

#include "cmui/c_srle.h"

namespace ncore
{
    namespace nrle
    {
        // ------------------------------------------------------------
        // internal bit helpers
        // ------------------------------------------------------------
        static inline u8 read_bit(const u8* data, u32& bit_pos)
        {
            u8 bit = (data[bit_pos >> 3] >> (bit_pos & 7)) & 1;
            bit_pos++;
            return bit;
        }

        static inline void write_bit(u8* data, u32& bit_pos, u32 bit)
        {
            if (bit)
                data[bit_pos >> 3] |= (1u << (bit_pos & 7));
            bit_pos++;
        }

        static mode_t choose_mode(u32 bit_count, u32 ones, u32 run_bits)
        {
            const u64 rhs = (u64)bit_count * run_bits;

            if ((u64)ones * (run_bits + 1) >= rhs)
                return MODE_ONES;

            const u32 zeros = bit_count - ones;
            if ((u64)zeros * (run_bits + 1) >= rhs)
                return MODE_ZEROS;

            return MODE_RAW_BITS;
        }

        // ------------------------------------------------------------
        // Encoder
        // ------------------------------------------------------------
        u32 encode_bits(const u8* bits, u32 bit_count, u32 run_bits, out_t& out, header_t& header)
        {
            u32 ones = 0;
            for (u32 i = 0; i < bit_count; ++i)
                ones += (bits[i] != 0);

            header.mode     = (u8)choose_mode(bit_count, ones, run_bits);
            header.run_bits = (u8)run_bits;

            u32 bit_pos = 0;

            if (header.mode == MODE_RAW_BITS)
            {
                for (u32 i = 0; i < bit_count; ++i)
                    write_bit(out.data, bit_pos, bits[i]);
                out.size = (bit_pos + 7) >> 3;
                return bit_pos;
            }

            const u32 rle_symbol = (header.mode == MODE_ONES) ? 1 : 0;
            const u32 max_run    = (1u << run_bits) - 1;

            for (u32 i = 0; i < bit_count;)
            {
                if (bits[i] != rle_symbol)
                {
                    write_bit(out.data, bit_pos, bits[i]);
                    i++;
                    continue;
                }

                u32 run = 0;
                while (i < bit_count && bits[i] == rle_symbol && run < max_run)
                {
                    run++;
                    i++;
                }

                write_bit(out.data, bit_pos, rle_symbol);
                for (u32 b = 0; b < run_bits; ++b)
                    write_bit(out.data, bit_pos, (run >> b) & 1);
            }

            out.size = (bit_pos + 7) >> 3;
            return bit_pos;
        }

        // ------------------------------------------------------------
        // Decoder (bulk)
        // ------------------------------------------------------------
        void decode_bits(const u8* in_bits, u32 in_bit_cnt, u8* out_bits, u32 out_bit_cnt, const header_t& header)
        {
            u32 in_pos = 0;

            if (header.mode == MODE_RAW_BITS)
            {
                for (u32 i = 0; i < out_bit_cnt; ++i)
                    out_bits[i] = read_bit(in_bits, in_pos);
                return;
            }

            const u32 rle_symbol = (header.mode == MODE_ONES) ? 1 : 0;

            for (u32 i = 0; i < out_bit_cnt;)
            {
                u8 bit = read_bit(in_bits, in_pos);
                if (bit != rle_symbol)
                {
                    out_bits[i++] = bit;
                    continue;
                }

                u32 run = 0;
                for (u32 b = 0; b < header.run_bits; ++b)
                    run |= read_bit(in_bits, in_pos) << b;

                for (u32 k = 0; k < run && i < out_bit_cnt; ++k)
                    out_bits[i++] = rle_symbol;
            }
        }

        // ------------------------------------------------------------
        // RAW reader
        // ------------------------------------------------------------
        void mode_raw_reader_init(bit_reader_t& r, const u8* data, u32 decoded_bit_count)
        {
            r.data           = data;
            r.bit_pos        = 0;
            r.run_bits       = 0;
            r.pending        = 0;
            r.remaining_bits = decoded_bit_count;
        }

        s8 mode_raw_read_bit(bit_reader_t& r)
        {
            if (r.remaining_bits == 0)
                return -1;

            r.remaining_bits--;
            return read_bit(r.data, r.bit_pos);
        }

        // ------------------------------------------------------------
        // RLE_ZEROS reader
        // ------------------------------------------------------------
        void mode_zeros_reader_init(bit_reader_t& r, const u8* data, u32 run_bits, u32 decoded_bit_count)
        {
            r.data           = data;
            r.bit_pos        = 0;
            r.run_bits       = run_bits;
            r.pending        = 0;
            r.remaining_bits = decoded_bit_count;
        }

        s8 mode_zeros_read_bit(bit_reader_t& r)
        {
            if (r.remaining_bits == 0)
                return -1;
            r.remaining_bits--;

            if (r.pending > 0)
            {
                r.pending--;
                return 0;
            }

            u8 bit = read_bit(r.data, r.bit_pos);
            if (bit == 1)
                return 1;

            u32 run = 0;
            for (u32 b = 0; b < r.run_bits; ++b)
                run |= read_bit(r.data, r.bit_pos) << b;

            r.pending = run - 1;
            return 0;
        }

        // ------------------------------------------------------------
        // RLE_ONES reader
        // ------------------------------------------------------------
        void mode_ones_reader_init(bit_reader_t& r, const u8* data, u32 run_bits, u32 decoded_bit_count)
        {
            r.data           = data;
            r.bit_pos        = 0;
            r.run_bits       = run_bits;
            r.pending        = 0;
            r.remaining_bits = decoded_bit_count;
        }

        s8 mode_ones_read_bit(bit_reader_t& r)
        {
            if (r.remaining_bits == 0)
                return -1;
            r.remaining_bits--;

            if (r.pending > 0)
            {
                r.pending--;
                return 1;
            }

            u8 bit = read_bit(r.data, r.bit_pos);
            if (bit == 0)
                return 0;

            u32 run = 0;
            for (u32 b = 0; b < r.run_bits; ++b)
                run |= read_bit(r.data, r.bit_pos) << b;

            r.pending = run - 1;
            return 1;
        }
    }  // namespace nrle
}  // namespace ncore
