#ifndef WINJECT_ETHERNET_H_
#define WINJECT_ETHERNET_H_

#include <stdint.h>

#include "esp_netif.h"
#include "bfc-esp32/semaphore.hpp"

class ethernet
{
public:
    static ethernet& instance();
    ethernet(const ethernet&) = delete;
    ethernet& operator=(const ethernet&) = delete;
    virtual ~ethernet() = default;

    // dma_burst_beats: EMAC DMA burst (tune set_eth_dma_burst_len).
    virtual bool begin(uint8_t dma_burst_beats) = 0;
    virtual bool ready() const = 0;
    virtual bool connected() const = 0;
    // PHY link up (not true for static fallback without cable).
    virtual bool link_up() const = 0;
    virtual void set_connected(bool connected) = 0;
    virtual bool using_static() const = 0;
    virtual void set_using_static(bool using_static) = 0;
    virtual esp_netif_t* netif() = 0;
    virtual bfc::semaphore& mutex() = 0;

    virtual bool has_ipv4() const = 0;
    // ip / netmask in network byte order. Stops the DHCP client.
    virtual bool apply_static_ip(uint32_t ip, uint32_t netmask) = 0;

    virtual bool local_ipv4(uint32_t* out) = 0;
    virtual bool mac(uint8_t mac[6]) = 0;
    virtual bool link_speed_mbps(uint32_t* mbps) = 0;

protected:
    ethernet() = default;
};

#endif  // WINJECT_ETHERNET_H_
