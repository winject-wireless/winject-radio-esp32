#include "ether_test.h"

#include "test_pacer.h"

#include <errno.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lwip/sockets.h"

static const char* TAG = "ether_test";

mplane_status ether_test::set_rx_port(uint16_t port)
{
    if (!rx_lock_.init())
    {
        ESP_LOGE(TAG, "rx lock alloc failed");
        return mplane_status::io_error;
    }
    {
        bfc::semaphore::lock guard(rx_lock_);
        if (!guard)
        {
            return mplane_status::io_error;
        }
        rx_sock_.close();
        if (port == 0)
        {
            ESP_LOGI(TAG, "rx closed");
            return mplane_status::ok;
        }
        if (!rx_sock_.open_udp(0, port))
        {
            ESP_LOGE(TAG, "rx bind *:%u failed: %d", static_cast<unsigned>(port),
                     errno);
            return mplane_status::io_error;
        }
        rx_sock_.set_sock_opt(SOL_SOCKET, SO_RCVBUF,
                              static_cast<int>(ETHER_TEST_SOCK_BUF));
    }
    if (rx_task_ == nullptr &&
        xTaskCreatePinnedToCore(rx_task, "ether_test_rx", ETHER_TEST_TASK_STACK,
                                this, ETHER_TEST_RX_TASK_PRIO, &rx_task_,
                                APP_TASK_CORE) != pdPASS)
    {
        rx_task_ = nullptr;
        ESP_LOGE(TAG, "rx task create failed (heap_free=%u)",
                 static_cast<unsigned>(
                     heap_caps_get_free_size(MALLOC_CAP_8BIT)));
        bfc::semaphore::lock guard(rx_lock_);
        rx_sock_.close();
        return mplane_status::io_error;
    }
    xTaskNotifyGive(rx_task_);
    ESP_LOGI(TAG, "rx *:%u", static_cast<unsigned>(port));
    return mplane_status::ok;
}

rx_test_stats ether_test::rx_stats(bool clear)
{
    rx_test_stats out;
    if (clear)
    {
        out.pkt = rx_pkt_.exchange(0, std::memory_order_relaxed);
        out.byt = rx_byt_.exchange(0, std::memory_order_relaxed);
    }
    else
    {
        out.pkt = rx_pkt_.load(std::memory_order_relaxed);
        out.byt = rx_byt_.load(std::memory_order_relaxed);
    }
    return out;
}

void ether_test::rx_task(void* arg)
{
    static_cast<ether_test*>(arg)->run_rx();
}

void ether_test::run_rx()
{
    for (;;)
    {
        bool open = false;
        {
            bfc::semaphore::lock guard(rx_lock_);
            open = rx_sock_.valid();
            // Bounded batch: the console may be waiting to rebind, and IDLE1
            // must run to pet the task WDT.
            for (int i = 0; open && i < ETHER_TEST_YIELD_EVERY; ++i)
            {
                const ssize_t n = rx_sock_.recv(rx_buf_, sizeof(rx_buf_));
                if (n < 0)
                {
                    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                    {
                        ESP_LOGW(TAG, "recv failed: %d", errno);
                    }
                    break;
                }
                rx_pkt_.fetch_add(1, std::memory_order_relaxed);
                rx_byt_.fetch_add(static_cast<uint64_t>(n),
                                  std::memory_order_relaxed);
            }
        }
        if (!open)
        {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        vTaskDelay(1);
    }
}

mplane_status ether_test::tx(const ether_tx_request& req)
{
    if (req.count == 0)
    {
        return tx_run_.stop(req.id);
    }
    if (req.host == 0 || req.port == 0 || req.mtu == 0 ||
        req.mtu > ETHER_TEST_MTU_MAX)
    {
        return mplane_status::invalid;
    }
    const mplane_status st = tx_run_.begin(req.id);
    if (st != mplane_status::ok)
    {
        return st;
    }
    tx_req_ = req;
    if (xTaskCreatePinnedToCore(tx_task, "ether_test_tx", ETHER_TEST_TASK_STACK,
                                this, ETHER_TEST_TX_TASK_PRIO, nullptr,
                                APP_TASK_CORE) != pdPASS)
    {
        tx_run_.abort_begin();
        ESP_LOGE(TAG, "tx task create failed (heap_free=%u)",
                 static_cast<unsigned>(
                     heap_caps_get_free_size(MALLOC_CAP_8BIT)));
        return mplane_status::io_error;
    }
    return mplane_status::ok;
}

void ether_test::tx_task(void* arg)
{
    static_cast<ether_test*>(arg)->run_tx();
    vTaskDelete(nullptr);
}

void ether_test::run_tx()
{
    const ether_tx_request req = tx_req_;
    bfc::socket sock;
    if (!sock.open_udp(0, 0))
    {
        ESP_LOGE(TAG, "tx socket failed: %d", errno);
        tx_run_.finish();
        return;
    }
    sock.set_sock_opt(SOL_SOCKET, SO_SNDBUF,
                      static_cast<int>(ETHER_TEST_SOCK_BUF));
    sockaddr_in dest = {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(req.port);
    dest.sin_addr.s_addr = req.host;

    memset(tx_buf_, 0, req.mtu);
    test_pacer pacer(test_frame_interval_us(req.mtu, req.rate_kbps));
    const int64_t t0 = esp_timer_get_time();
    uint32_t sent = 0;
    uint32_t since_yield = 0;
    while (sent < req.count && !tx_run_.stop_requested())
    {
        pacer.wait();
        // Sequence number lets a capture spot loss / reordering.
        memcpy(tx_buf_, &sent, req.mtu < sizeof(sent) ? req.mtu : sizeof(sent));
        const ssize_t n =
            sock.send(tx_buf_, req.mtu, 0,
                      reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
        if (n == static_cast<ssize_t>(req.mtu))
        {
            ++sent;
        }
        else if (n < 0 &&
                 (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOMEM))
        {
            vTaskDelay(1);
            continue;
        }
        else
        {
            ESP_LOGW(TAG, "tx sendto failed after %u frames: %d",
                     static_cast<unsigned>(sent), errno);
            break;
        }
        if (++since_yield >= ETHER_TEST_YIELD_EVERY)
        {
            since_yield = 0;
            vTaskDelay(1);
        }
    }
    ESP_LOGI(TAG, "tx id=%u done sent=%u/%u us=%lld",
             static_cast<unsigned>(req.id), static_cast<unsigned>(sent),
             static_cast<unsigned>(req.count),
             static_cast<long long>(esp_timer_get_time() - t0));
    tx_run_.finish();
}
