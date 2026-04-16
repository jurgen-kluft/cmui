#include "ccore/c_target.h"

#include "cbsdf/c_bsdf.h"

#include "cunittest/cunittest.h"

using namespace ncore;

namespace ncore
{
    namespace nbsdf
    {
        const u8 test_data[] = {
          0x46,        0xB1, 0xD5,
          0x2C,         // magic 'c', 'b', 's', 'd', 'f' in 6 bit encoding
          0x01,         // major version
          0x00,         // minor version
          0x00,         // build version
          TYPE_OBJECT,  // type 'o' for object
        };
    }  // namespace nbsdf
}  // namespace ncore

UNITTEST_SUITE_BEGIN(bsdf)
{
    UNITTEST_FIXTURE(main)
    {
        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(test_header)
        {
            nbsdf::header_t header;
            u32 offset = 0;
            const bool result = nbsdf::read_header(nbsdf::test_data, offset, header);
            CHECK_TRUE(result);
            CHECK_EQUAL(ENCODE6_32('c', 'b', 's', 'd', 'f'), header.magic);
            CHECK_EQUAL(1, header.major_version );
            CHECK_EQUAL(0, header.minor_version );
            CHECK_EQUAL(0, header.build_version );
            CHECK_EQUAL(nbsdf::TYPE_OBJECT, header.type);
        }
    }
}
UNITTEST_SUITE_END
