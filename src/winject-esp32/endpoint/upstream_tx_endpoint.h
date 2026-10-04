#ifndef WINJECT_UPSTREAM_TX_ENDPOINT_H_
#define WINJECT_UPSTREAM_TX_ENDPOINT_H_

#include <atomic>
#include <stdint.h>

#include "esp_eth.h"
#include "esp_netif.h"

class wifi_tx;

// D-plane on DPLANE_PORT: 24–1472 byte UDP payloads are hijacked at L2 for
// inject (queued on wifi_tx without a copy). 1–23 byte payloads are left for
// lwIP (peer registration on the same port). Everything else goes to the netif.
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
    // lwip_addr: esp_ip4_addr_t::addr (network byte order), 0 = none.
    void set_local_ipv4(uint32_t lwip_addr);

    uint32_t ether_pkt() const;

private:
    upstream_tx_endpoint() = default;

    static esp_err_t eth_input_cb(esp_eth_handle_t eth, uint8_t* buffer,
                                  uint32_t length, void* priv);
    bool try_hijack_udp(uint8_t* buffer, uint32_t length);

    std::atomic<wifi_tx*> tx_{nullptr};
    esp_netif_t* netif_ = nullptr;
    uint8_t mac_[6]{};
    std::atomic<uint32_t> local_ip_host_{0};
    std::atomic<uint32_t> ether_pkt_{0};
};

#endif  // WINJECT_UPSTREAM_TX_ENDPOINT_H_
