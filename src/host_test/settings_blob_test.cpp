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

namespace
{
class legacy_blob_builder
{
public:
    legacy_blob_builder& version(uint8_t v)
    {
        buf_[0] = v;
        return *this;
    }

    legacy_blob_builder& mode(uint8_t m)
    {
        buf_[1] = m;
        return *this;
    }

    legacy_blob_builder& channel(uint8_t ch)
    {
        buf_[2] = ch;
        return *this;
    }

    legacy_blob_builder& cca(uint8_t v)
    {
        buf_[3] = v;
        return *this;
    }

    legacy_blob_builder& allow_failed_crc(uint8_t v)
    {
        buf_[4] = v;
        return *this;
    }

    legacy_blob_builder& tx_power(int8_t dbm)
    {
        buf_[5] = static_cast<uint8_t>(dbm);
        return *this;
    }

    legacy_blob_builder& modulation(const char* name)
    {
        memset(buf_ + 6, 0, SETTINGS_MODULATION_MAX);
        strncpy(reinterpret_cast<char*>(buf_ + 6), name,
                SETTINGS_MODULATION_MAX);
        return *this;
    }

    legacy_blob_builder& modulation_raw(const uint8_t* bytes, size_t n)
    {
        memset(buf_ + 6, 0, SETTINGS_MODULATION_MAX);
        memcpy(buf_ + 6, bytes, n > SETTINGS_MODULATION_MAX ? SETTINGS_MODULATION_MAX
                                                            : n);
        return *this;
    }

    legacy_blob_builder& ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
    {
        buf_[22] = a;
        buf_[23] = b;
        buf_[24] = c;
        buf_[25] = d;
        return *this;
    }

    legacy_blob_builder& network_mode(uint8_t m)
    {
        buf_[26] = m;
        buf_[27] = 0;  // legacy dhcp_server
        return *this;
    }

    legacy_blob_builder& domain(uint16_t d)
    {
        buf_[28] = static_cast<uint8_t>(d);
        buf_[29] = static_cast<uint8_t>(d >> 8);
        return *this;
    }

    legacy_blob_builder& v7_tail_has_sut(uint16_t port)
    {
        buf_[30] = 1;
        buf_[31] = static_cast<uint8_t>(port);
        buf_[32] = static_cast<uint8_t>(port >> 8);
        buf_[33] = 0;
        len_ = 34;
        return *this;
    }

    legacy_blob_builder& v5_tables(uint8_t sut_count, uint8_t sur_count)
    {
        size_t p = 30;
        buf_[p++] = sut_count;
        for (uint8_t i = 0; i < sut_count; i++)
        {
            buf_[p++] = 0;
            buf_[p++] = 0;
            buf_[p++] = 0;
        }
        buf_[p++] = sur_count;
        for (uint8_t i = 0; i < sur_count; i++)
        {
            buf_[p++] = 0;
            buf_[p++] = 0;
            buf_[p++] = 0;
            buf_[p++] = 0;
            buf_[p++] = 0;
            buf_[p++] = 0;
        }
        buf_[p++] = 0;
        len_ = p;
        return *this;
    }

    legacy_blob_builder& length(size_t n)
    {
        len_ = n;
        return *this;
    }

    const uint8_t* data() const
    {
        return buf_;
    }

    size_t size() const
    {
        return len_;
    }

private:
    uint8_t buf_[512] = {};
    size_t len_ = 30;
};
}  // namespace

TEST(SettingsBlobTest, LegacyV7StaticIp)
{
    const legacy_blob_builder b =
        legacy_blob_builder()
            .version(7)
            .channel(6)
            .cca(0)
            .tx_power(15)
            .modulation("OFDM_24M")
            .ip(10, 0, 0, 42)
            .network_mode(0)
            .domain(0)
            .v7_tail_has_sut(1234);

    settings_snapshot snap;
    snap.network.ip = 0xdeadbeef;
    ASSERT_TRUE(settings_blob_unpack_legacy(b.data(), b.size(), &snap));
    EXPECT_EQ(snap.network.type, network_type::static_ip);
    const uint8_t expect_ip[4] = {10, 0, 0, 42};
    EXPECT_EQ(memcmp(&snap.network.ip, expect_ip, 4), 0);
    EXPECT_EQ(snap.network.prefix, 24);
    EXPECT_EQ(snap.network.timeout_s, NETWORK_DHCP_TIMEOUT_S_DEFAULT);
    EXPECT_EQ(snap.radio.channel, 6);
    EXPECT_EQ(snap.radio.tx_power_dbm, 15);
    EXPECT_STREQ(snap.radio.modulation, "OFDM_24M");
    EXPECT_FALSE(snap.radio.cca_enabled);
}

TEST(SettingsBlobTest, LegacyV7Auto)
{
    const legacy_blob_builder b =
        legacy_blob_builder()
            .version(7)
            .channel(1)
            .tx_power(10)
            .modulation("CCK_11M_L")
            .ip(192, 168, 32, 1)
            .network_mode(1)
            .domain(0)
            .v7_tail_has_sut(0);

    settings_snapshot snap;
    ASSERT_TRUE(settings_blob_unpack_legacy(b.data(), b.size(), &snap));
    EXPECT_EQ(snap.network.type, network_type::dhcp);
    const uint8_t expect_ip[4] = {192, 168, 32, 1};
    EXPECT_EQ(memcmp(&snap.network.ip, expect_ip, 4), 0);
}

TEST(SettingsBlobTest, LegacyV4NoTail)
{
    const legacy_blob_builder b =
        legacy_blob_builder()
            .version(4)
            .channel(11)
            .tx_power(20)
            .modulation("OFDM_54M")
            .ip(1, 2, 3, 4)
            .network_mode(0)
            .domain(0)
            .length(30);

    settings_snapshot snap;
    ASSERT_TRUE(settings_blob_unpack_legacy(b.data(), b.size(), &snap));
    EXPECT_EQ(snap.radio.channel, 11);
}

TEST(SettingsBlobTest, LegacyV5WithTables)
{
    const legacy_blob_builder b =
        legacy_blob_builder()
            .version(5)
            .channel(3)
            .tx_power(8)
            .modulation("CCK_5_5M")
            .ip(10, 10, 10, 10)
            .network_mode(1)
            .domain(0)
            .v5_tables(2, 1);

    settings_snapshot snap;
    ASSERT_TRUE(settings_blob_unpack_legacy(b.data(), b.size(), &snap));
    EXPECT_EQ(snap.network.type, network_type::dhcp);
    EXPECT_EQ(snap.radio.channel, 3);
}

TEST(SettingsBlobTest, LegacyZeroIpKeepsDefault)
{
    const legacy_blob_builder b =
        legacy_blob_builder()
            .version(7)
            .channel(6)
            .tx_power(15)
            .modulation("OFDM_24M")
            .ip(0, 0, 0, 0)
            .network_mode(0)
            .domain(0)
            .v7_tail_has_sut(1);

    settings_snapshot snap;
    snap.network.ip = 0x01020304;
    ASSERT_TRUE(settings_blob_unpack_legacy(b.data(), b.size(), &snap));
    EXPECT_EQ(snap.network.ip, 0x01020304u);
}

TEST(SettingsBlobTest, LegacyModulationNoNul)
{
    uint8_t raw[SETTINGS_MODULATION_MAX];
    memset(raw, 'X', sizeof(raw));
    const legacy_blob_builder b =
        legacy_blob_builder()
            .version(6)
            .channel(6)
            .tx_power(12)
            .modulation_raw(raw, sizeof(raw))
            .ip(10, 0, 0, 1)
            .network_mode(0)
            .domain(0)
            .length(30);

    settings_snapshot snap;
    ASSERT_TRUE(settings_blob_unpack_legacy(b.data(), b.size(), &snap));
    EXPECT_EQ(snap.radio.modulation[SETTINGS_MODULATION_MAX - 1], '\0');
    EXPECT_EQ(snap.radio.modulation[SETTINGS_MODULATION_MAX - 2], 'X');
}

TEST(SettingsBlobTest, LegacyRejectsBadInput)
{
    settings_snapshot snap;
    uint8_t buf[64] = {};

    buf[0] = 2;
    EXPECT_FALSE(settings_blob_unpack_legacy(buf, 30, &snap));

    buf[0] = 8;
    EXPECT_FALSE(settings_blob_unpack_legacy(buf, 30, &snap));

    buf[0] = 9;
    EXPECT_FALSE(settings_blob_unpack_legacy(buf, 30, &snap));

    buf[0] = 7;
    EXPECT_FALSE(settings_blob_unpack_legacy(buf, 27, &snap));

    buf[26] = 2;
    EXPECT_FALSE(settings_blob_unpack_legacy(buf, 30, &snap));
}

TEST(SettingsBlobTest, V9UnpackRejectsLegacySizedV6)
{
    uint8_t buf[k_settings_blob_size] = {};
    buf[0] = 6;
    settings_snapshot out;
    EXPECT_FALSE(settings_blob_unpack(buf, sizeof(buf), &out));
}
