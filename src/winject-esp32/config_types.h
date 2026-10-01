#ifndef WINJECT_CONFIG_TYPES_H_
#define WINJECT_CONFIG_TYPES_H_

#include "config.h"

#include <stddef.h>
#include <stdint.h>

// Plain configuration values shared by the m-plane, settings slots, and the
// host simulator. No ESP-IDF dependencies.

enum class network_type : uint8_t
{
    dhcp = 0,
    static_ip = 1,
};

// ip is network byte order. With type=dhcp, ip/prefix is the static fallback
// applied after timeout_s without a lease (0 = never fall back).
struct network_config
{
    network_type type = network_type::dhcp;
    uint32_t ip = 0;
    uint8_t prefix = NETWORK_PREFIX_DEFAULT;
    uint16_t timeout_s = NETWORK_DHCP_TIMEOUT_S_DEFAULT;
};

// Netmask for prefix 0..32, network byte order.
uint32_t network_prefix_netmask(uint8_t prefix);
// Unicast host address (not 0/8, 127/8, multicast/reserved) and, for prefixes
// up to /30, neither the network nor the broadcast address.
bool network_config_valid(const network_config& cfg);

struct radio_config
{
    uint8_t channel = WIFI_DEFAULT_CHANNEL;
    int8_t tx_power_dbm = WIFI_DEFAULT_TX_POWER_DBM;
    char modulation[SETTINGS_MODULATION_MAX] = WIFI_DEFAULT_MODULATION;
    bool cca_enabled = true;
};

// Exact 6-byte MAC match; a disabled filter matches everything.
struct mac_filter
{
    bool enabled = false;
    uint8_t addr[6] = {};

    bool matches(const uint8_t* mac) const;
};

// One-word form so a filter shared with the WiFi RX callback can live in a
// single std::atomic<uint64_t>. 0 = disabled.
uint64_t mac_filter_pack(const mac_filter& filter);
mac_filter mac_filter_unpack(uint64_t word);

// Boot-time sizing. Values take effect on the next boot after `save`.
struct tune_config
{
    uint8_t eth_dma_burst_len = 32;
    uint8_t eth_rx_ring_sz = 0;
    uint8_t eth_tx_ring_sz = 0;
    uint8_t tx_queue_sz = WIFI_TX_QUEUE_DEFAULT;
    uint8_t rx_queue_sz = WIFI_RX_QUEUE_DEFAULT;
    uint8_t wifi_tx_ring_sz = 0;
    uint8_t wifi_rx_ring_sz = 0;
};

// Build-time constraints a tune_config is checked against. EMAC descriptor
// counts are compiled into ESP-IDF (CONFIG_ETH_DMA_*_BUFFER_NUM), so the
// eth ring sizes are accepted only at their built value.
struct tune_limits
{
    uint8_t eth_rx_ring_sz = 0;
    uint8_t eth_tx_ring_sz = 0;
    uint8_t wifi_rx_ring_min = 0;
};

bool tune_eth_dma_burst_valid(uint8_t beats);
bool tune_config_valid(const tune_config& cfg, const tune_limits& limits);

#endif  // WINJECT_CONFIG_TYPES_H_
