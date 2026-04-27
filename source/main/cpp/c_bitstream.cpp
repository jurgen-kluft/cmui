#include "ccore/c_target.h"
#include "ccore/c_memory.h"

#include "cmui/c_bitstream.h"

namespace ncore
{
    namespace nbitstream
    {
        // ------------------------------------------------------------
        // Writer
        // ------------------------------------------------------------

        void init(writer_t* bs, u8* buffer, u32 capacity_bits)
        {
            bs->buf           = buffer;
            bs->capacity_bits = capacity_bits;
            bs->num_bits      = 0;
            bs->pos           = 0;
            bs->accu_num_bits = 0;
            bs->accu_register = 0;
            bs->finalized     = false;
        }

        s8 write_bits(writer_t* bs, u32 value, u8 num_bits)
        {
            if (num_bits == 0 || num_bits > 30 || bs->finalized)
                return -1;

            if ((bs->num_bits + num_bits) > bs->capacity_bits)
                return -1;

            // accumulate bits (LSB-first)
            bs->accu_register |= (u64(value & ((1u << num_bits) - 1u)) << bs->accu_num_bits);
            bs->accu_num_bits += num_bits;
            bs->num_bits += num_bits;

            // flush 32 bits at once
            if (bs->accu_num_bits >= 32)
            {
                for (u32 i = 0; i < 4; ++i)
                {
                    bs->buf[bs->pos++] = (u8)(bs->accu_register);
                    bs->accu_register >>= 8;
                    bs->accu_num_bits -= 8;
                }
            }

            return 0;
        }

        u32 finalize(writer_t* bs)
        {
            // flush remaining bits byte-by-byte
            const u32 num_bytes_to_flush = (bs->accu_num_bits + 7) / 8;  // round up to nearest byte
            for (u32 i = 0; i < num_bytes_to_flush; ++i)
            {
                bs->buf[bs->pos++] = (u8)(bs->accu_register & 0xFF);
                bs->accu_register >>= 8;
            }

            bs->accu_num_bits = 0;
            bs->accu_register = 0;
            bs->finalized     = true;
            return bs->num_bits;
        }

        // ------------------------------------------------------------
        // Reader
        // ------------------------------------------------------------

        void init(reader_t* bs, const u8* buffer, u32 num_bits)
        {
            bs->buf           = buffer;
            bs->num_bits      = num_bits;
            bs->read_bits     = 0;
            bs->pos           = 0;
            bs->accu_num_bits = 0;
            bs->accu_register = 0;
        }

        void reset(reader_t* bs)
        {
            bs->read_bits     = 0;
            bs->pos           = 0;
            bs->accu_num_bits = 0;
            bs->accu_register = 0;
        }

        static inline void fill(reader_t* bs)
        {
            while (bs->accu_num_bits < 32 && (bs->pos * 8) < bs->num_bits)
            {
                bs->accu_register |= u64(bs->buf[bs->pos]) << bs->accu_num_bits;
                bs->accu_num_bits += 8;
                bs->pos++;
            }
        }

        s32 read_bits(reader_t* bs, u8 num_bits)
        {
            if (num_bits == 0 || num_bits > 30)
                return -1;

            if ((bs->read_bits + num_bits) > bs->num_bits)
                return -1;

            fill(bs);

            const u32 mask = (1u << num_bits) - 1u;
            const u32 v    = (u32)(bs->accu_register & mask);

            bs->accu_register >>= num_bits;
            bs->accu_num_bits -= num_bits;
            bs->read_bits += num_bits;

            return (s32)v;
        }

        s32 peek_bits(reader_t* bs, u8 num_bits)
        {
            if (num_bits == 0 || num_bits > 30)
                return -1;

            if ((bs->read_bits + num_bits) > bs->num_bits)
                return -1;

            fill(bs);

            const u32 mask = (1u << num_bits) - 1u;
            return (s32)(bs->accu_register & mask);
        }

        s8 skip_bits(reader_t* bs, u8 num_bits)
        {
            if (num_bits == 0 || num_bits > 30)
                return -1;

            if ((bs->read_bits + num_bits) > bs->num_bits)
                return -1;

            fill(bs);
            bs->accu_register >>= num_bits;
            bs->accu_num_bits -= num_bits;
            bs->read_bits += num_bits;
            return 0;
        }

        bool is_end(const reader_t* bs, u8 sizeof_symbol_bits)
        {
            if (bs->read_bits >= bs->num_bits)
                return true;

            return (bs->num_bits - bs->read_bits) < sizeof_symbol_bits;
        }

    }  // namespace nbitstream
}  // namespace ncore
