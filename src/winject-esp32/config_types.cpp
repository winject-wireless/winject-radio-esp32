#include "config_types.h"

#include <string.h>

namespace
{
uint32_t to_host_order(uint32_t nbo)
{
    const auto* b = reinterpret_cast<const uint8_t*>(&nbo);
    return (static_cast<uint32_t>(b[0]) << 24) |
           (static_cast<uint32_t>(b[1]) << 16) |
           (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
}

uint32_t to_net_order(uint32_t host)
{
    uint32_t out = 0;
    auto* b = reinterpret_cast<uint8_t*>(&out);
    b[0] = static_cast<uint8_t>(host >> 24);
    b[1] = static_cast<uint8_t>(host >> 16);
    b[2] = static_cast<uint8_t>(host >> 8);
    b[3] = static_cast<uint8_t>(host);
    return out;
}
}  // namespace

uint32_t network_prefix_netmask(uint8_t prefix)
{
    if (prefix == 0)
    {
        return 0;
    }
    if (prefix >= 32)
    {
        return 0xFFFFFFFFu;
    }
    return to_net_order(~((1u << (32u - prefix)) - 1u));
}

bool network_config_valid(const network_config& cfg)
{
    if (cfg.type != network_type::dhcp && cfg.type != network_type::static_ip)
    {
        return false;
    }
    if (cfg.prefix < 1 || cfg.prefix > 32)
    {
        return false;
    }
    const uint32_t ip = to_host_order(cfg.ip);
    const uint8_t first = static_cast<uint8_t>(ip >> 24);
    if (first == 0 || first == 127 || first >= 224)
    {
        return false;
    }
    if (cfg.prefix <= 30)
    {
        const uint32_t host_mask = (1u << (32u - cfg.prefix)) - 1u;
        const uint32_t host = ip & host_mask;
        if (host == 0 || host == host_mask)
        {
            return false;
        }
    }
    return true;
}

bool mac_filter::matches(const uint8_t* mac) const
{
    if (!enabled)
    {
        return true;
    }
    return mac != nullptr && memcmp(mac, addr, sizeof(addr)) == 0;
}

uint64_t mac_filter_pack(const mac_filter& filter)
{
    if (!filter.enabled)
    {
        return 0;
    }
    uint64_t word = 1ull << 48;
    for (size_t i = 0; i < sizeof(filter.addr); ++i)
    {
        word |= static_cast<uint64_t>(filter.addr[i]) << (8 * i);
    }
    return word;
}

mac_filter mac_filter_unpack(uint64_t word)
{
    mac_filter out;
    out.enabled = (word >> 48) != 0;
    if (out.enabled)
    {
        for (size_t i = 0; i < sizeof(out.addr); ++i)
        {
            out.addr[i] = static_cast<uint8_t>(word >> (8 * i));
        }
    }
    return out;
}

bool tune_eth_dma_burst_valid(uint8_t beats)
{
    return beats == 1 || beats == 2 || beats == 4 || beats == 8 ||
           beats == 16 || beats == 32;
}

bool tune_config_valid(const tune_config& cfg, const tune_limits& limits)
{
    return tune_eth_dma_burst_valid(cfg.eth_dma_burst_len) &&
           cfg.eth_rx_ring_sz == limits.eth_rx_ring_sz &&
           cfg.eth_tx_ring_sz == limits.eth_tx_ring_sz &&
           cfg.tx_queue_sz >= 1 && cfg.tx_queue_sz <= WIFI_TX_QUEUE_MAX &&
           cfg.rx_queue_sz >= 1 && cfg.rx_queue_sz <= WIFI_RX_QUEUE_MAX &&
           cfg.wifi_tx_ring_sz >= WIFI_TX_RING_MIN &&
           cfg.wifi_tx_ring_sz <= WIFI_TX_RING_MAX &&
           cfg.wifi_rx_ring_sz >= limits.wifi_rx_ring_min &&
           cfg.wifi_rx_ring_sz <= WIFI_RX_RING_MAX;
}
