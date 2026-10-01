#include "settings_blob.h"

#include <gtest/gtest.h>

#include <string.h>

namespace
{
settings_snapshot sample()
{
    settings_snapshot s;
    s.network.type = network_type::static_ip;
    const uint8_t ip[4] = {10, 1, 2, 3};
    memcpy(&s.network.ip, ip, sizeof(ip));
    s.network.prefix = 16;
    s.network.timeout_s = 300;
    s.radio.channel = 14;
    s.radio.tx_power_dbm = 11;
    strcpy(s.radio.modulation, "CCK_11M_S");
    s.radio.cca_enabled = false;
    s.rx_filter.enabled = true;
    const uint8_t mac[6] = {0xca, 0xfe, 0xba, 0xbe, 0x12, 0x34};
    memcpy(s.rx_filter.addr, mac, sizeof(mac));
    s.tune.eth_dma_burst_len = 8;
    s.tune.eth_rx_ring_sz = 28;
    s.tune.eth_tx_ring_sz = 16;
    s.tune.tx_queue_sz = 40;
    s.tune.rx_queue_sz = 12;
    s.tune.wifi_tx_ring_sz = 24;
    s.tune.wifi_rx_ring_sz = 48;
    return s;
}
}  // namespace

TEST(SettingsBlobTest, RoundTrip)
{
    const settings_snapshot in = sample();
    uint8_t buf[k_settings_blob_size];
    ASSERT_EQ(settings_blob_pack(in, buf, sizeof(buf)), k_settings_blob_size);
    EXPECT_EQ(buf[0], k_settings_blob_version);

    settings_snapshot out;
    ASSERT_TRUE(settings_blob_unpack(buf, sizeof(buf), &out));
    EXPECT_EQ(out.network.type, in.network.type);
    EXPECT_EQ(out.network.ip, in.network.ip);
    EXPECT_EQ(out.network.prefix, in.network.prefix);
    EXPECT_EQ(out.network.timeout_s, in.network.timeout_s);
    EXPECT_EQ(out.radio.channel, in.radio.channel);
    EXPECT_EQ(out.radio.tx_power_dbm, in.radio.tx_power_dbm);
    EXPECT_STREQ(out.radio.modulation, in.radio.modulation);
    EXPECT_EQ(out.radio.cca_enabled, in.radio.cca_enabled);
    EXPECT_EQ(out.rx_filter.enabled, in.rx_filter.enabled);
    EXPECT_EQ(memcmp(out.rx_filter.addr, in.rx_filter.addr, 6), 0);
    EXPECT_EQ(out.tune.eth_dma_burst_len, in.tune.eth_dma_burst_len);
    EXPECT_EQ(out.tune.tx_queue_sz, in.tune.tx_queue_sz);
    EXPECT_EQ(out.tune.rx_queue_sz, in.tune.rx_queue_sz);
    EXPECT_EQ(out.tune.wifi_tx_ring_sz, in.tune.wifi_tx_ring_sz);
    EXPECT_EQ(out.tune.wifi_rx_ring_sz, in.tune.wifi_rx_ring_sz);
}

TEST(SettingsBlobTest, NegativeTxPowerSurvives)
{
    settings_snapshot in = sample();
    in.radio.tx_power_dbm = -2;
    uint8_t buf[k_settings_blob_size];
    ASSERT_NE(settings_blob_pack(in, buf, sizeof(buf)), 0u);
    settings_snapshot out;
    ASSERT_TRUE(settings_blob_unpack(buf, sizeof(buf), &out));
    EXPECT_EQ(out.radio.tx_power_dbm, -2);
}

TEST(SettingsBlobTest, PackRejectsSmallBuffer)
{
    uint8_t buf[k_settings_blob_size - 1];
    EXPECT_EQ(settings_blob_pack(sample(), buf, sizeof(buf)), 0u);
}

TEST(SettingsBlobTest, UnpackRejectsOtherVersionsAndLengths)
{
    uint8_t buf[k_settings_blob_size + 1];
    ASSERT_NE(settings_blob_pack(sample(), buf, sizeof(buf)), 0u);
    settings_snapshot out;
    EXPECT_FALSE(settings_blob_unpack(buf, k_settings_blob_size - 1, &out));
    EXPECT_FALSE(settings_blob_unpack(buf, k_settings_blob_size + 1, &out));
    buf[0] = 8;
    EXPECT_FALSE(settings_blob_unpack(buf, k_settings_blob_size, &out));
}

TEST(SettingsBlobTest, UnpackRejectsCorruptFields)
{
    uint8_t good[k_settings_blob_size];
    ASSERT_NE(settings_blob_pack(sample(), good, sizeof(good)), 0u);
    settings_snapshot out;

    uint8_t bad[k_settings_blob_size];
    memcpy(bad, good, sizeof(bad));
    bad[1] = 7;  // network type
    EXPECT_FALSE(settings_blob_unpack(bad, sizeof(bad), &out));

    memcpy(bad, good, sizeof(bad));
    memset(bad + 12, 'A', SETTINGS_MODULATION_MAX);  // no NUL
    EXPECT_FALSE(settings_blob_unpack(bad, sizeof(bad), &out));

    memcpy(bad, good, sizeof(bad));
    bad[12 + SETTINGS_MODULATION_MAX] = 2;  // filter enabled flag
    EXPECT_FALSE(settings_blob_unpack(bad, sizeof(bad), &out));
}
