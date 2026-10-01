#ifndef WINJECT_UPSTREAM_TX_ENDPOINT_H_
#define WINJECT_UPSTREAM_TX_ENDPOINT_H_

#include <atomic>
#include <stdint.h>

#include "esp_eth.h"
#include "esp_netif.h"

class wifi_tx;

// D-plane inject: EMAC input frames that are IPv4/UDP to DPLANE_INJECT_PORT
// are taken out of the lwIP path and their payload (one raw MPDU) is queued on
// wifi_tx without a copy. Everything else goes to the netif.
class upstream_tx_endpoint
{
public:
    static upstream_tx_endpoint& instance();
    upstream_tx_endpoint(const upstream_tx_endpoint&) = delete;
    upstream_tx_endpoint& operator=(const upstream_tx_endpoint&) = delete;

    // Installed by Ethernet bring-up; inject frames reach the netif until
    // set_sink() runs (and forever in OTA mode).
    void attach_eth_input(esp_eth_handle_t eth, esp_netif_t* netif);
    void set_sink(wifi_tx& tx);
    void set_local_ipv4(uint32_t ip_be);

private:
    upstream_tx_endpoint() = default;

    static esp_err_t eth_input_cb(esp_eth_handle_t eth, uint8_t* buffer,
                                  uint32_t length, void* priv);
    bool try_hijack_udp(uint8_t* buffer, uint32_t length);

    std::atomic<wifi_tx*> tx_{nullptr};
    esp_netif_t* netif_ = nullptr;
    uint8_t mac_[6]{};
    std::atomic<uint32_t> local_ip_be_{0};
};

#endif  // WINJECT_UPSTREAM_TX_ENDPOINT_H_
