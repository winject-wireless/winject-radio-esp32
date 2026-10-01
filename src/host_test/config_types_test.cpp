#include "config_types.h"

#include <gtest/gtest.h>

#include <string.h>

namespace
{
uint32_t ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    uint32_t out = 0;
    auto* p = reinterpret_cast<uint8_t*>(&out);
    p[0] = a;
    p[1] = b;
    p[2] = c;
    p[3] = d;
    return out;
}

tune_limits limits()
{
    tune_limits l;
    l.eth_rx_ring_sz = 28;
    l.eth_tx_ring_sz = 16;
    l.wifi_rx_ring_min = 8;
    return l;
}

tune_config valid_tune()
{
    tune_config t;
    t.eth_rx_ring_sz = 28;
    t.eth_tx_ring_sz = 16;
    t.wifi_tx_ring_sz = 16;
    t.wifi_rx_ring_sz = 32;
    return t;
}
}  // namespace

TEST(NetworkConfigTest, NetmaskFromPrefix)
{
    EXPECT_EQ(network_prefix_netmask(24), ip(255, 255, 255, 0));
    EXPECT_EQ(network_prefix_netmask(16), ip(255, 255, 0, 0));
    EXPECT_EQ(network_prefix_netmask(30), ip(255, 255, 255, 252));
    EXPECT_EQ(network_prefix_netmask(32), ip(255, 255, 255, 255));
    EXPECT_EQ(network_prefix_netmask(0), 0u);
}

TEST(NetworkConfigTest, AcceptsUnicastHost)
{
    network_config cfg;
    cfg.ip = ip(192, 168, 32, 1);
    cfg.prefix = 24;
    EXPECT_TRUE(network_config_valid(cfg));
    cfg.type = network_type::static_ip;
    cfg.prefix = 16;
    EXPECT_TRUE(network_config_valid(cfg));
}

TEST(NetworkConfigTest, RejectsNetworkAndBroadcastAddress)
{
    network_config cfg;
    cfg.prefix = 24;
    cfg.ip = ip(192, 168, 32, 0);
    EXPECT_FALSE(network_config_valid(cfg));
    cfg.ip = ip(192, 168, 32, 255);
    EXPECT_FALSE(network_config_valid(cfg));
    // .255 is a host on a /16.
    cfg.prefix = 16;
    EXPECT_TRUE(network_config_valid(cfg));
}

TEST(NetworkConfigTest, RejectsReservedRangesAndBadPrefix)
{
    network_config cfg;
    cfg.ip = ip(127, 0, 0, 1);
    EXPECT_FALSE(network_config_valid(cfg));
    cfg.ip = ip(224, 0, 0, 1);
    EXPECT_FALSE(network_config_valid(cfg));
    cfg.ip = ip(0, 1, 2, 3);
    EXPECT_FALSE(network_config_valid(cfg));
    cfg.ip = ip(10, 0, 0, 1);
    cfg.prefix = 0;
    EXPECT_FALSE(network_config_valid(cfg));
    cfg.prefix = 33;
    EXPECT_FALSE(network_config_valid(cfg));
    cfg.prefix = 32;
    EXPECT_TRUE(network_config_valid(cfg));
}

TEST(MacFilterTest, DisabledMatchesEverything)
{
    const mac_filter f;
    const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    EXPECT_TRUE(f.matches(mac));
}

TEST(MacFilterTest, EnabledRequiresExactMatch)
{
    mac_filter f;
    f.enabled = true;
    const uint8_t want[6] = {0xca, 0xfe, 0xba, 0xbe, 0x00, 0x01};
    memcpy(f.addr, want, sizeof(want));
    EXPECT_TRUE(f.matches(want));
    const uint8_t other[6] = {0xca, 0xfe, 0xba, 0xbe, 0x00, 0x02};
    EXPECT_FALSE(f.matches(other));
    EXPECT_FALSE(f.matches(nullptr));
}

TEST(MacFilterTest, PackRoundTrip)
{
    EXPECT_EQ(mac_filter_pack(mac_filter{}), 0u);
    EXPECT_FALSE(mac_filter_unpack(0).enabled);

    mac_filter f;
    f.enabled = true;
    // All-zero address must stay distinguishable from "disabled".
    EXPECT_NE(mac_filter_pack(f), 0u);
    EXPECT_TRUE(mac_filter_unpack(mac_filter_pack(f)).enabled);

    const uint8_t mac[6] = {0xff, 0x01, 0x80, 0x7f, 0x00, 0xca};
    memcpy(f.addr, mac, sizeof(mac));
    const mac_filter back = mac_filter_unpack(mac_filter_pack(f));
    EXPECT_TRUE(back.enabled);
    EXPECT_EQ(memcmp(back.addr, mac, sizeof(mac)), 0);
}

TEST(TuneConfigTest, AcceptsDefaultsAtBuildLimits)
{
    EXPECT_TRUE(tune_config_valid(valid_tune(), limits()));
}

TEST(TuneConfigTest, DmaBurstMustBePowerOfTwoUpTo32)
{
    tune_config t = valid_tune();
    for (uint8_t beats : {1, 2, 4, 8, 16, 32})
    {
        t.eth_dma_burst_len = beats;
        EXPECT_TRUE(tune_config_valid(t, limits())) << int(beats);
    }
    for (uint8_t beats : {0, 3, 64})
    {
        t.eth_dma_burst_len = beats;
        EXPECT_FALSE(tune_config_valid(t, limits())) << int(beats);
    }
}

TEST(TuneConfigTest, EthRingsFixedAtBuildValue)
{
    tune_config t = valid_tune();
    t.eth_rx_ring_sz = 27;
    EXPECT_FALSE(tune_config_valid(t, limits()));
    t = valid_tune();
    t.eth_tx_ring_sz = 17;
    EXPECT_FALSE(tune_config_valid(t, limits()));
}

TEST(TuneConfigTest, QueueAndRingBounds)
{
    tune_config t = valid_tune();
    t.tx_queue_sz = 0;
    EXPECT_FALSE(tune_config_valid(t, limits()));
    t.tx_queue_sz = WIFI_TX_QUEUE_MAX;
    EXPECT_TRUE(tune_config_valid(t, limits()));
    t.tx_queue_sz = WIFI_TX_QUEUE_MAX + 1;
    EXPECT_FALSE(tune_config_valid(t, limits()));

    t = valid_tune();
    t.rx_queue_sz = WIFI_RX_QUEUE_MAX + 1;
    EXPECT_FALSE(tune_config_valid(t, limits()));

    t = valid_tune();
    t.wifi_rx_ring_sz = 7;
    EXPECT_FALSE(tune_config_valid(t, limits()));
    t.wifi_rx_ring_sz = WIFI_RX_RING_MAX + 1;
    EXPECT_FALSE(tune_config_valid(t, limits()));

    t = valid_tune();
    t.wifi_tx_ring_sz = 0;
    EXPECT_FALSE(tune_config_valid(t, limits()));
}
