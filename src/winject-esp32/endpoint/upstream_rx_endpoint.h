#ifndef WINJECT_UPSTREAM_RX_ENDPOINT_H_
#define WINJECT_UPSTREAM_RX_ENDPOINT_H_

#include "config.h"

#include <atomic>
#include <stddef.h>
#include <stdint.h>

#include "bfc-esp32/socket.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class wifi_rx;

// D-plane RX: drains wifi_rx and sends each frame (MPDU + FCS) as one UDP
// datagram from DPLANE_RX_PORT to the peer that last sent any datagram there.
class upstream_rx_endpoint
{
public:
    static upstream_rx_endpoint& instance();
    upstream_rx_endpoint(const upstream_rx_endpoint&) = delete;
    upstream_rx_endpoint& operator=(const upstream_rx_endpoint&) = delete;

    // Binds DPLANE_RX_PORT and starts the drain task (once).
    bool start(wifi_rx& rx, BaseType_t core = APP_TASK_CORE,
               UBaseType_t prio = UPSTREAM_RX_TASK_PRIO,
               uint32_t stack_bytes = UPSTREAM_RX_TASK_STACK);

    uint32_t ether_pkt() const;
    uint32_t dropped_no_peer() const;
    uint32_t dropped_send_failed() const;

private:
    upstream_rx_endpoint() = default;

    static void drain_task(void* arg);
    void run_drain();

    void poll_peer();
    void send_one(const uint8_t* data, size_t len);

    wifi_rx* rx_ = nullptr;
    TaskHandle_t drain_task_handle_ = nullptr;
    // Drain task only after start().
    bool peer_valid_ = false;
    uint32_t peer_host_ = 0;
    uint16_t peer_port_ = 0;
    int64_t last_peer_poll_us_ = 0;
    bfc::socket sock_;
    std::atomic<uint32_t> ether_pkt_{0};
    std::atomic<uint32_t> dropped_no_peer_{0};
    std::atomic<uint32_t> dropped_send_failed_{0};
};

#endif  // WINJECT_UPSTREAM_RX_ENDPOINT_H_
