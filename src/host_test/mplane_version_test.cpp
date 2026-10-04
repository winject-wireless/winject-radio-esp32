#include "mplane_version.h"

#include <gtest/gtest.h>

TEST(MplaneVersionTest, ParseOk)
{
    uint8_t x = 0;
    uint8_t y = 0;
    uint16_t z = 0;
    EXPECT_TRUE(mplane_parse_version("v1.0.3", &x, &y, &z));
    EXPECT_EQ(x, 1);
    EXPECT_EQ(y, 0);
    EXPECT_EQ(z, 3);
}

TEST(MplaneVersionTest, ParseRejected)
{
    uint8_t x = 0;
    uint8_t y = 0;
    uint16_t z = 0;
    EXPECT_FALSE(mplane_parse_version("1.0.3", &x, &y, &z));
    EXPECT_FALSE(mplane_parse_version("v1.0", &x, &y, &z));
    EXPECT_FALSE(mplane_parse_version("v1.0.3x", &x, &y, &z));
    EXPECT_FALSE(mplane_parse_version("v256.0.0", &x, &y, &z));
}
