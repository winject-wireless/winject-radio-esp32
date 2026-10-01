#include "upstream_rx_endpoint.h"

#include "control_peer.h"
#include "packet.h"
#include "wifi_rx.h"

#include <errno.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"

static const char* TAG = "dplane_rx";

upstream_rx_endpoint& upstream_rx_endpoint::instance()
{
    static upstream_rx_endpoint inst;
    return inst;
}

void upstream_rx_endpoint::poll_peer()
{
    char buf[64];
    for (;;)
    {
        sockaddr_in from = {};
        socklen_t from_len = sizeof(from);
        const ssize_t n =
            recvfrom(sock_.fd(), buf, sizeof(buf), 0,
                     reinterpret_cast<sockaddr*>(&from), &from_len);
        if (n < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
                ESP_LOGD(TAG, "peer poll recv: %d", errno);
            }
            return;
        }
        if (from.sin_addr.s_addr == 0 || from.sin_port == 0)
        {
            continue;
        }
        if (!control_peer_allowed(from.sin_addr.s_addr))
        {
            continue;
        }
        if (!peer_valid_ || peer_host_ != from.sin_addr.s_addr ||
            peer_port_ != from.sin_port)
        {
            char host[16];
            inet_ntoa_r(from.sin_addr, host, sizeof(host));
            ESP_LOGI(TAG, "peer %s:%u", host,
                     static_cast<unsigned>(ntohs(from.sin_port)));
        }
        peer_host_ = from.sin_addr.s_addr;
        peer_port_ = from.sin_port;
        peer_valid_ = true;
    }
}

void upstream_rx_endpoint::send_one(const uint8_t* data, size_t len)
{
    if (!peer_valid_)
    {
        return;
    }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = peer_host_;
    addr.sin_port = peer_port_;
    const ssize_t n =
        sock_.send(data, len, 0, reinterpret_cast<const sockaddr*>(&addr),
                   sizeof(addr));
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
    {
        ESP_LOGD(TAG, "sendto failed: %d", errno);
    }
}

void upstream_rx_endpoint::run_drain()
{
    for (;;)
    {
        packet frame = rx_->pop(portMAX_DELAY);
        if (!frame.is_valid())
        {
            continue;
        }
        rx_->on_upstream_deliver();
        const int64_t now = esp_timer_get_time();
        if (!peer_valid_ || now - last_peer_poll_us_ >= 10000)
        {
            poll_peer();
            last_peer_poll_us_ = now;
        }
        send_one(frame.data(), frame.size());
    }
}

void upstream_rx_endpoint::drain_task(void* arg)
{
    static_cast<upstream_rx_endpoint*>(arg)->run_drain();
}

bool upstream_rx_endpoint::start(wifi_rx& rx, BaseType_t core,
                                 UBaseType_t prio, uint32_t stack_bytes)
{
    if (drain_task_handle_ != nullptr)
    {
        return true;
    }
    if (!sock_.open_udp(0, DPLANE_RX_PORT))
    {
        ESP_LOGE(TAG, "udp bind %u failed: %d", DPLANE_RX_PORT, errno);
        sock_.close();
        return false;
    }
    rx_ = &rx;
    if (xTaskCreatePinnedToCore(drain_task, "dplane_rx", stack_bytes, this,
                                prio, &drain_task_handle_, core) != pdPASS)
    {
        ESP_LOGE(TAG, "drain task create failed");
        sock_.close();
        drain_task_handle_ = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "air->UDP %u drain core=%d prio=%u", DPLANE_RX_PORT,
             static_cast<int>(core), static_cast<unsigned>(prio));
    return true;
}
