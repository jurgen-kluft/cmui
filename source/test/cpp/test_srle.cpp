#include "ccore/c_target.h"

#include "cmui/c_srle.h"
#include "cmui/c_bitstream.h"

#include "cunittest/cunittest.h"

using namespace ncore;

UNITTEST_SUITE_BEGIN(srle)
{
    static u32 pack_symbols(const u8* symbols, u32 count, u8 symbol_bits, u8* buffer, u32 buffer_size)
    {
        nbitstream::writer_t writer;
        nbitstream::init(&writer, buffer, buffer_size * 8);
        for (u32 i = 0; i < count; ++i)
            nbitstream::write_bits(&writer, symbols[i], symbol_bits);
        return nbitstream::finalize(&writer);
    }

    static bool check_buffers_equal(const u8* expected, const u8* actual, u32 num_bytes)
    {
        for (u32 i = 0; i < num_bytes; ++i)
            if (expected[i] != actual[i])
                return false;
        return true;
    }

    UNITTEST_FIXTURE(encode)
    {
        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(rejects_invalid_symbol_sizes)
        {
            nrle::encoder_t encoder = {};
            const u8       data[1] = {0};

            CHECK_EQUAL(-1, nrle::analyze_bits(&encoder, data, 8, 0));
            CHECK_EQUAL(-1, nrle::analyze_bits(&encoder, data, 8, 3));
            CHECK_EQUAL(-1, nrle::analyze_bits(&encoder, data, 8, 7));

            nrle::header_t header = {};
            header.m_symbol_bits  = 3;
            nrle::decoder_t decoder = {};
            CHECK_EQUAL(-1, nrle::decoder_init(decoder, &header, data));
        }

        UNITTEST_TEST(analyze_selects_run_bits_for_repeated_symbols)
        {
            const u8 symbols[] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
            u8       source[8] = {0};
            const u32 data_bits = pack_symbols(symbols, (u32)(sizeof(symbols) / sizeof(symbols[0])), 2, source, sizeof(source));

            nrle::encoder_t encoder = {};
            CHECK_EQUAL(1, nrle::analyze_bits(&encoder, source, data_bits, 2));
            CHECK_EQUAL((u32)6, encoder.m_encoded_size_in_bits);
            CHECK_EQUAL(4, encoder.m_symbol_rb[3]);
            CHECK_EQUAL(0, encoder.m_symbol_rb[0]);
            CHECK_EQUAL(0, encoder.m_symbol_rb[1]);
            CHECK_EQUAL(0, encoder.m_symbol_rb[2]);
        }

        UNITTEST_TEST(roundtrips_alternating_symbols_in_raw_mode)
        {
            const u8 symbols[] = {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};
            u8       source[8] = {0};
            const u32 data_bits = pack_symbols(symbols, (u32)(sizeof(symbols) / sizeof(symbols[0])), 2, source, sizeof(source));

            nrle::encoder_t encoder = {};
            CHECK_EQUAL(3, nrle::analyze_bits(&encoder, source, data_bits, 2));

            nrle::header_t header = {};
            u8             encoded[8] = {0};
            nrle::out_t    encoded_out = {encoded, (u32)sizeof(encoded)};
            const s32      encoded_bits = nrle::encode_bits(&encoder, header, encoded_out);

            CHECK_EQUAL((s32)data_bits, encoded_bits);
            CHECK_EQUAL(data_bits, nrle::decoded_size(&header));
            CHECK_EQUAL(0, nrle::symbol_rb(&header, 0));
            CHECK_EQUAL(0, nrle::symbol_rb(&header, 1));
            CHECK_EQUAL(-1, nrle::symbol_rb(&header, 4));

            nrle::decoder_t decoder = {};
            CHECK_EQUAL(0, nrle::decoder_init(decoder, &header, encoded));

            u8          decoded[8] = {0};
            nrle::out_t decoded_out = {decoded, (u32)sizeof(decoded)};
            CHECK_EQUAL((s32)data_bits, nrle::decode_all(decoder, decoded_out));
            CHECK_TRUE(check_buffers_equal(source, decoded, (data_bits + 7) >> 3));
        }

        UNITTEST_TEST(roundtrips_long_runs_with_chunking)
        {
            u8 symbols[44] = {0};
            for (u32 i = 0; i < 40; ++i)
                symbols[i] = 2;
            symbols[40] = 3;
            symbols[41] = 1;
            symbols[42] = 0;
            symbols[43] = 2;

            u8       source[16] = {0};
            const u32 data_bits = pack_symbols(symbols, DARRAYSIZE(symbols), 2, source, sizeof(source));

            nrle::encoder_t encoder = {};
            const u32 expected_encoded_size = ((2 + 5) + (2 + 5) + 2 + 2 + 2 + (2 + 5) + 7) / 8;
            CHECK_EQUAL(expected_encoded_size, (u32)nrle::analyze_bits(&encoder, source, data_bits, 2));
            CHECK_EQUAL(0, encoder.m_symbol_rb[0]);
            CHECK_EQUAL(0, encoder.m_symbol_rb[1]);
            CHECK_EQUAL(5, encoder.m_symbol_rb[2]);
            CHECK_EQUAL(0, encoder.m_symbol_rb[3]);

            nrle::header_t header = {};
            u8             encoded[16] = {0};
            nrle::out_t    encoded_out = {encoded, (u32)sizeof(encoded)};
            const s32      encoded_bits = nrle::encode_bits(&encoder, header, encoded_out);

            CHECK_TRUE(encoded_bits > 0);
            CHECK_TRUE(encoded_bits < (s32)data_bits);
            CHECK_EQUAL(5, nrle::symbol_rb(&header, 2));
            CHECK_EQUAL(0, nrle::symbol_rb(&header, 0));

            nrle::decoder_t decoder = {};
            CHECK_EQUAL(0, nrle::decoder_init(decoder, &header, encoded));

            u8          decoded[16] = {0};
            nrle::out_t decoded_out = {decoded, (u32)sizeof(decoded)};
            CHECK_EQUAL((s32)data_bits, nrle::decode_all(decoder, decoded_out));
            check_buffers_equal(source, decoded, (data_bits + 7) >> 3);
        }

    }
}
UNITTEST_SUITE_END
