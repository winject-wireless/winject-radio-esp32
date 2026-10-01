#include "fcs.h"

#include <gtest/gtest.h>

#include <string.h>

TEST(WifiFcsTest, MatchesStandardCrc32CheckValue)
{
    const char* check = "123456789";
    EXPECT_EQ(wifi_fcs_compute(reinterpret_cast<const uint8_t*>(check),
                               strlen(check)),
              0xCBF43926u);
}

TEST(WifiFcsTest, StoredFcsVerifies)
{
    uint8_t frame[32 + 4] = {};
    for (size_t i = 0; i < 32; ++i)
    {
        frame[i] = static_cast<uint8_t>(i * 7);
    }
    wifi_fcs_store(frame, 32, frame + 32);
    EXPECT_TRUE(wifi_fcs_matches(frame, sizeof(frame)));
    // FCS is little-endian on the wire.
    const uint32_t crc = wifi_fcs_compute(frame, 32);
    EXPECT_EQ(frame[32], static_cast<uint8_t>(crc));
    EXPECT_EQ(frame[35], static_cast<uint8_t>(crc >> 24));
}

TEST(WifiFcsTest, DetectsCorruption)
{
    uint8_t frame[24 + 4] = {0x08, 0x00};
    wifi_fcs_store(frame, 24, frame + 24);
    frame[10] ^= 0x01;
    EXPECT_FALSE(wifi_fcs_matches(frame, sizeof(frame)));
}

TEST(WifiFcsTest, RejectsShortInput)
{
    const uint8_t b[3] = {};
    EXPECT_FALSE(wifi_fcs_matches(b, sizeof(b)));
    EXPECT_FALSE(wifi_fcs_matches(nullptr, 8));
}
