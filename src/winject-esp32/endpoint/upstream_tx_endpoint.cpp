#include "upstream_tx_endpoint.h"

#include "config.h"
#include "packet.h"
#include "udp_l2_match.h"
#include "wifi_tx.h"

#include <stdlib.h>
#include <utility>

#include "esp_log.h"

static const char* TAG = "upstream_tx";

upstream_tx_endpoint& upstream_tx_endpoint::instance()
{
    static upstream_tx_endpoint inst;
    return inst;
}

void upstream_tx_endpoint::set_local_ipv4(uint32_t lwip_addr)
{
    local_ip_host_.store(udp_l2_ipv4_host_from_lwip(lwip_addr),
                         std::memory_order_relaxed);
}

uint32_t upstream_tx_endpoint::ether_pkt() const
{
    return ether_pkt_.load(std::memory_order_relaxed);
}

bool upstream_tx_endpoint::try_hijack_udp(uint8_t* buffer, uint32_t length)
{
    wifi_tx* tx = tx_.load(std::memory_order_acquire);
    const uint8_t* payload = nullptr;
    uint16_t payload_len = 0;
    const uint32_t dst_ip_host =
        local_ip_host_.load(std::memory_order_relaxed);
    if (tx == nullptr ||
        !udp_l2_match_dst(buffer, length, mac_, dst_ip_host,
                          DPLANE_INJECT_PORT, &payload, &payload_len))
    {
        return false;
    }
    ether_pkt_.fetch_add(1, std::memory_order_relaxed);
    // Matched frames are consumed: from here the packet owns buffer, so a
    // dropped frame must not also be handed to lwIP.
    if (payload_len < WIFI_RADIO_INJECT_MIN || payload_len > WIFI_RADIO_INJECT_MAX)
    {
        tx->note_invalid_frame();
        free(buffer);
        return true;
    }
    tx->enqueue(packet::adopt_heap(buffer, payload, payload_len));
    return true;
}

esp_err_t upstream_tx_endpoint::eth_input_cb(esp_eth_handle_t eth,
                                             uint8_t* buffer, uint32_t length,
                                             void* priv)
{
    (void)eth;
    auto* self = static_cast<upstream_tx_endpoint*>(priv);
    if (self == nullptr || buffer == nullptr)
    {
        free(buffer);
        return ESP_ERR_INVALID_ARG;
    }
    if (self->try_hijack_udp(buffer, length))
    {
        return ESP_OK;
    }
    if (self->netif_ == nullptr)
    {
        free(buffer);
        return ESP_ERR_INVALID_STATE;
    }
    return esp_netif_receive(self->netif_, buffer, length, nullptr);
}

void upstream_tx_endpoint::attach_eth_input(esp_eth_handle_t eth,
                                            esp_netif_t* netif)
{
    if (eth == nullptr || netif == nullptr)
    {
        return;
    }
    netif_ = netif;
    if (esp_eth_ioctl(eth, ETH_CMD_G_MAC_ADDR, mac_) != ESP_OK)
    {
        ESP_LOGW(TAG, "read eth MAC failed");
    }
    const esp_err_t err =
        esp_eth_update_input_path(eth, eth_input_cb, this);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "eth input hijack failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "eth L2 inject hijack installed");
}

void upstream_tx_endpoint::set_sink(wifi_tx& tx)
{
    tx_.store(&tx, std::memory_order_release);
    ESP_LOGI(TAG, "d-plane inject UDP %u -> wifi_tx", DPLANE_INJECT_PORT);
}
