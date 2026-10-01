#include "wifi_tx_test.h"

#include "config.h"
#include "frame.h"
#include "packet.h"
#include "test_pacer.h"
#include "wifi.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "wifi_tx_test";

namespace
{
void fill_frame(uint8_t* buf, size_t len, const mac_address& addr1,
                const mac_address& addr2, const mac_address& addr3,
                uint16_t seq)
{
    memset(buf, 0, len);
    buf[0] = 0x08;  // data frame, no flags
    memcpy(buf + 4, addr1.data(), addr1.size());
    memcpy(buf + 10, addr2.data(), addr2.size());
    memcpy(buf + 16, addr3.data(), addr3.size());
    const uint16_t seq_ctl = static_cast<uint16_t>((seq & 0x0FFF) << 4);
    buf[22] = static_cast<uint8_t>(seq_ctl);
    buf[23] = static_cast<uint8_t>(seq_ctl >> 8);
}
}  // namespace

wifi_tx_test::wifi_tx_test(wifi& radio) : radio_(radio) {}

mplane_status wifi_tx_test::run(const wifi_tx_request& req)
{
    if (req.count == 0)
    {
        return run_.stop(req.id);
    }
    if (!radio_.ready())
    {
        return mplane_status::no_device;
    }
    if (req.mtu < WIFI_RADIO_INJECT_MIN || req.mtu > WIFI_RADIO_INJECT_MAX)
    {
        return mplane_status::invalid;
    }
    const mplane_status st = run_.begin(req.id);
    if (st != mplane_status::ok)
    {
        return st;
    }
    req_ = req;
    if (xTaskCreatePinnedToCore(task, "wifi_tx_test", WIFI_TX_TEST_TASK_STACK,
                                this, WIFI_TX_TEST_TASK_PRIO, nullptr,
                                APP_TASK_CORE) != pdPASS)
    {
        run_.abort_begin();
        ESP_LOGE(TAG, "task create failed (heap_free=%u)",
                 static_cast<unsigned>(
                     heap_caps_get_free_size(MALLOC_CAP_8BIT)));
        return mplane_status::io_error;
    }
    return mplane_status::ok;
}

void wifi_tx_test::task(void* arg)
{
    static_cast<wifi_tx_test*>(arg)->run_task();
    vTaskDelete(nullptr);
}

void wifi_tx_test::run_task()
{
    const wifi_tx_request req = req_;
    mac_address own{};
    frameGetStaMac(own.data());
    const mac_address addr1 = req.addr1.value_or(
        mac_address{0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    const mac_address addr2 = req.addr2.value_or(own);
    const mac_address addr3 = req.addr3.value_or(mac_address{});

    wifi_tx& tx = radio_.tx();
    test_pacer pacer(test_frame_interval_us(req.mtu, req.rate_kbps));
    const int64_t t0 = esp_timer_get_time();
    uint32_t sent = 0;
    uint32_t alloc_fail = 0;
    while (sent < req.count && !run_.stop_requested())
    {
        if (tx.queue_full())
        {
            vTaskDelay(1);
            continue;
        }
        pacer.wait();
        // wifi_tx frees the frame after 80211_tx copies it.
        auto* buf = static_cast<uint8_t*>(malloc(req.mtu));
        if (buf == nullptr)
        {
            ++alloc_fail;
            vTaskDelay(1);
            continue;
        }
        fill_frame(buf, req.mtu, addr1, addr2, addr3,
                   static_cast<uint16_t>(sent));
        if (!tx.enqueue(packet::adopt_heap(buf, buf, req.mtu)))
        {
            vTaskDelay(1);
            continue;
        }
        if (++sent % ETHER_TEST_YIELD_EVERY == 0)
        {
            vTaskDelay(1);
        }
    }
    ESP_LOGI(TAG, "done sent=%u/%u alloc_fail=%u us=%lld",
             static_cast<unsigned>(sent), static_cast<unsigned>(req.count),
             static_cast<unsigned>(alloc_fail),
             static_cast<long long>(esp_timer_get_time() - t0));
    run_.finish();
}
