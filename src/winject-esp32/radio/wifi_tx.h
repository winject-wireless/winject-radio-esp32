#ifndef WINJECT_WIFI_TX_H_
#define WINJECT_WIFI_TX_H_

#include "config.h"
#include "packet.h"

#include <atomic>
#include <optional>
#include <stddef.h>
#include <stdint.h>

#include "bfc-esp32/wait_free_queue.hpp"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"

class wifi;

// Raw 802.11 inject: producers enqueue MPDUs, one task submits them to
// esp_wifi_80211_tx with at most WIFI_RADIO_MAX_IN_FLIGHT outstanding.
class wifi_tx
{
    friend class wifi;

public:
    static constexpr uint8_t k_queue_max = WIFI_TX_QUEUE_MAX;

    wifi_tx(const wifi_tx&) = delete;
    wifi_tx& operator=(const wifi_tx&) = delete;

    // capacity: tune tx_queue_sz (1..k_queue_max). Call once before start().
    bool init(uint8_t capacity);
    bool start(BaseType_t core = WIFI_RADIO_TASK_CORE,
               UBaseType_t prio = WIFI_RADIO_TASK_PRIO,
               uint32_t stack_bytes = 6144);

    // Any task (EMAC RX, test_wifi_tx). Consumes pkt; on false it was dropped.
    bool enqueue(packet&& pkt);
    // Frames waiting for the inject task.
    uint8_t queue_size() const;
    bool queue_full() const;
    // Frames submitted to the driver whose TX-done has not fired yet.
    uint8_t in_flight() const;

    uint32_t dropped_invalid_frame() const;
    uint32_t dropped_tx_queue() const;
    uint32_t dropped_wifi() const;
    uint32_t air_pkt() const;
    void note_invalid_frame();

private:
    explicit wifi_tx(wifi& radio);

    void run();
    bool inject_retry(const uint8_t* frame, size_t len);
    void wait_for_driver_slot();
    bool release_driver_slot();
    void reset_in_flight();
    void note_inject_fail(esp_err_t err);

    // Called with wifi::lock held (console task).
    bool apply_power();
    bool apply_cca();
    bool apply_tx_done_cb();
    int8_t power_dbm() const;
    bool cca_enabled() const;
    bool set_cca_enabled(bool enabled);
    bool set_tx_power(int8_t dbm);

    static void task(void* arg);
    static void on_tx_done(const esp_80211_tx_info_t* info);

    wifi& radio;
    bfc::wait_free_queue<std::optional<packet>, k_queue_max> q;
    bool cca_enabled_ = true;
    bool phy_cca_off = false;
    int8_t tx_power_dbm = WIFI_DEFAULT_TX_POWER_DBM;
    std::atomic<uint32_t> in_flight_{0};
    std::atomic<uint32_t> dropped_invalid_frame_{0};
    std::atomic<uint32_t> dropped_tx_queue_{0};
    std::atomic<uint32_t> dropped_wifi_{0};
    std::atomic<uint32_t> air_pkt_{0};
    std::atomic<int64_t> last_progress_us_{0};
    uint8_t burst_sent_ = 0;
    uint32_t fail_count_ = 0;
    int64_t fail_log_us_ = 0;
    TaskHandle_t task_handle_ = nullptr;
};

#endif  // WINJECT_WIFI_TX_H_
