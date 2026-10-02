#include "wifi_tx.h"

#include "config.h"
#include "packet.h"
#include "wifi.h"

#include <utility>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bfc-esp32/semaphore.hpp"

static const char* TAG = "wifi_tx";

static constexpr int64_t k_fail_log_interval_us = 1000000;

extern "C" int ieee80211_raw_frame_sanity_check(int32_t, int32_t, uint32_t,
                                                uint32_t)
{
    return 0;
}

extern "C"
{
    int hal_mac_tx_set_cca(int enable);
    void esp_rom_phy_disable_cca(void) __attribute__((weak));
    void phy_disable_cca(void) __attribute__((weak));
    void phy_enable_cca(void) __attribute__((weak));
}

wifi_tx::wifi_tx(wifi& radio) : radio(radio) {}

int8_t wifi_tx::power_dbm() const
{
    return tx_power_dbm;
}

bool wifi_tx::cca_enabled() const
{
    return cca_enabled_;
}

bool wifi_tx::apply_power()
{
    esp_err_t err = radio.apply_country();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "set_country failed: %s", esp_err_to_name(err));
        return false;
    }

    int8_t dbm = tx_power_dbm;
    const wifi_phy_rate_t rate = radio.phy_rate();
    if (rate == WIFI_PHY_RATE_48M || rate == WIFI_PHY_RATE_54M)
    {
        if (dbm > WIFI_TX_POWER_64QAM_LEGACY_MAX_DBM)
        {
            dbm = WIFI_TX_POWER_64QAM_LEGACY_MAX_DBM;
        }
    }

    const int8_t quarter_dbm = static_cast<int8_t>(dbm * 4);
    err = esp_wifi_set_max_tx_power(quarter_dbm);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "set_max_tx_power %d dBm failed: %s", dbm,
                 esp_err_to_name(err));
        return false;
    }
    return true;
}

bool wifi_tx::apply_cca()
{
    const int err = hal_mac_tx_set_cca(cca_enabled_ ? 1 : 0);
    if (err != 0)
    {
        ESP_LOGE(TAG, "hal_mac_tx_set_cca failed: %d", err);
        return false;
    }

    if (cca_enabled_)
    {
        if (phy_cca_off && phy_enable_cca)
        {
            phy_enable_cca();
            phy_cca_off = false;
        }
        return true;
    }

    if (esp_rom_phy_disable_cca)
    {
        esp_rom_phy_disable_cca();
        phy_cca_off = true;
    }
    else if (phy_disable_cca)
    {
        phy_disable_cca();
        phy_cca_off = true;
    }
    return true;
}

bool wifi_tx::init(uint8_t capacity)
{
    if (!q.init(capacity))
    {
        ESP_LOGE(TAG, "tx queue init capacity=%u failed (max %u)",
                 static_cast<unsigned>(capacity),
                 static_cast<unsigned>(k_queue_max));
        return false;
    }
    last_progress_us_.store(esp_timer_get_time(), std::memory_order_relaxed);
    return true;
}

uint8_t wifi_tx::in_flight() const
{
    return static_cast<uint8_t>(in_flight_.load(std::memory_order_relaxed));
}

uint32_t wifi_tx::dropped_invalid_frame() const
{
    return dropped_invalid_frame_.load(std::memory_order_relaxed);
}

uint32_t wifi_tx::dropped_tx_queue() const
{
    return dropped_tx_queue_.load(std::memory_order_relaxed);
}

uint32_t wifi_tx::dropped_wifi() const
{
    return dropped_wifi_.load(std::memory_order_relaxed);
}

uint32_t wifi_tx::air_pkt() const
{
    return air_pkt_.load(std::memory_order_relaxed);
}

void wifi_tx::note_invalid_frame()
{
    dropped_invalid_frame_.fetch_add(1, std::memory_order_relaxed);
}

static void delay_us(uint32_t us)
{
    if (us == 0)
    {
        return;
    }
    if (us >= 1000u)
    {
        vTaskDelay(pdMS_TO_TICKS((us + 999u) / 1000u));
        return;
    }
    esp_rom_delay_us(us);
}

bool wifi_tx::enqueue(packet&& pkt)
{
    if (!q.ready() || !pkt.is_valid())
    {
        pkt.reset();
        return false;
    }
    std::optional<packet> item(std::move(pkt));
    if (q.try_push(std::move(item)))
    {
        return true;
    }
    dropped_tx_queue_.fetch_add(1, std::memory_order_relaxed);
    if (item.has_value())
    {
        item->reset();
    }
    return false;
}

uint8_t wifi_tx::queue_size() const
{
    return q.size();
}

bool wifi_tx::queue_full() const
{
    return !q.ready() || q.size() >= q.limit();
}

void wifi_tx::reset_in_flight()
{
    const uint32_t prev = in_flight_.exchange(0, std::memory_order_relaxed);
    if (prev != 0)
    {
        dropped_wifi_.fetch_add(prev, std::memory_order_relaxed);
        ESP_LOGW(TAG, "reset in_flight from %u", static_cast<unsigned>(prev));
    }
    last_progress_us_.store(esp_timer_get_time(), std::memory_order_relaxed);
}

void wifi_tx::wait_for_driver_slot()
{
    // Bound outstanding TX so we do not storm NO_MEM and starve EMAC DMA.
    for (int spin = 0; in_flight_.load(std::memory_order_relaxed) >=
                       WIFI_RADIO_MAX_IN_FLIGHT;
         ++spin)
    {
        const int64_t now = esp_timer_get_time();
        const int64_t last =
            last_progress_us_.load(std::memory_order_relaxed);
        if (last > 0 && now - last > WIFI_TX_STALL_US)
        {
            reset_in_flight();
            break;
        }
        if (spin < WIFI_RADIO_INJECT_NOMEM_YIELD)
        {
            taskYIELD();
        }
        else
        {
            vTaskDelay(1);
            spin = 0;
        }
    }
}

bool wifi_tx::release_driver_slot()
{
    // Clamped: TX-done also fires for driver-originated frames.
    uint32_t n = in_flight_.load(std::memory_order_relaxed);
    while (n > 0 && !in_flight_.compare_exchange_weak(
                        n, n - 1, std::memory_order_relaxed))
    {
    }
    last_progress_us_.store(esp_timer_get_time(), std::memory_order_relaxed);
    return n > 0;
}

void wifi_tx::note_inject_fail(esp_err_t err)
{
    dropped_wifi_.fetch_add(1, std::memory_order_relaxed);
    ++fail_count_;
    const int64_t now = esp_timer_get_time();
    if (now - fail_log_us_ < k_fail_log_interval_us)
    {
        return;
    }
    fail_log_us_ = now;
    ESP_LOGW(TAG, "80211_tx dropped frame: %s (%u drops since last log)",
             esp_err_to_name(err), static_cast<unsigned>(fail_count_));
    fail_count_ = 0;
}

bool wifi_tx::inject_retry(const uint8_t* frame, size_t len)
{
    int fail_tries = 0;
    int nomem_tries = 0;
    int64_t nomem_start_us = 0;
    for (;;)
    {
        // Do not hold radio.lock across 80211_tx: that call posts into the
        // Wi-Fi task (same core as TX-done / promiscuous). A lock inversion
        // there stalls completions, so the driver ring never drains.
        wait_for_driver_slot();
        // Count before submitting: TX-done can run before 80211_tx returns.
        in_flight_.fetch_add(1, std::memory_order_relaxed);
        const esp_err_t err = esp_wifi_80211_tx(
            WIFI_IF_STA, frame, static_cast<int>(len), false);
        if (err == ESP_OK)
        {
            last_progress_us_.store(esp_timer_get_time(),
                                    std::memory_order_relaxed);
            return true;
        }
        release_driver_slot();
        if (err == ESP_ERR_NO_MEM)
        {
            const int64_t now = esp_timer_get_time();
            if (nomem_start_us == 0)
            {
                nomem_start_us = now;
            }
            else if (now - nomem_start_us > WIFI_RADIO_INJECT_NOMEM_MAX_US)
            {
                note_inject_fail(err);
                return false;
            }
            // Prefer yield over 1 ms sleep: TX-done on this core often frees
            // the driver ring within tens of µs.
            if (++nomem_tries <= WIFI_RADIO_INJECT_NOMEM_YIELD)
            {
                taskYIELD();
            }
            else
            {
                vTaskDelay(1);
            }
            continue;
        }
        if (++fail_tries >= WIFI_RADIO_INJECT_RETRIES)
        {
            note_inject_fail(err);
            return false;
        }
        if (fail_tries <= 2)
        {
            taskYIELD();
        }
        else
        {
            vTaskDelay(1);
        }
    }
}

void wifi_tx::run()
{
    for (;;)
    {
        std::optional<packet> slot;
        if (!q.pop(&slot, portMAX_DELAY) || !slot.has_value())
        {
            continue;
        }
        const packet& out = *slot;
        if (out.data() == nullptr || out.size() < WIFI_RADIO_INJECT_MIN ||
            out.size() > WIFI_RADIO_INJECT_MAX)
        {
            note_invalid_frame();
            continue;
        }

        if (inject_retry(out.data(), out.size()))
        {
            radio.pulse_tx_led();
        }

        burst_sent_++;
        if (burst_sent_ >= WIFI_TX_BURST_SIZE_DEFAULT)
        {
            delay_us(WIFI_TX_BURST_GAP_US);
            burst_sent_ = 0;
        }
        else if (queue_size() == 0)
        {
            burst_sent_ = 0;
        }
        else
        {
            taskYIELD();
        }
    }
}

void wifi_tx::task(void* arg)
{
    static_cast<wifi_tx*>(arg)->run();
}

bool wifi_tx::start(BaseType_t core, UBaseType_t prio, uint32_t stack_bytes)
{
    if (!q.ready())
    {
        ESP_LOGE(TAG, "wifi_tx start without queue");
        return false;
    }
    if (xTaskCreatePinnedToCore(task, "wifi_tx", stack_bytes, this, prio,
                                &task_handle_, core) != pdPASS)
    {
        ESP_LOGE(TAG, "wifi_tx task create failed");
        return false;
    }
    return true;
}

bool wifi_tx::set_cca_enabled(bool enabled)
{
    if (cca_enabled_ == enabled)
    {
        return true;
    }
    const bool previous = cca_enabled_;
    cca_enabled_ = enabled;
    if (!apply_cca())
    {
        cca_enabled_ = previous;
        return false;
    }
    return true;
}

bool wifi_tx::set_tx_power(int8_t dbm)
{
    if (dbm < WIFI_TX_POWER_DBM_MIN || dbm > WIFI_TX_POWER_DBM_MAX)
    {
        return false;
    }
    if (tx_power_dbm == dbm)
    {
        return true;
    }
    const int8_t previous = tx_power_dbm;
    tx_power_dbm = dbm;
    if (!apply_power())
    {
        tx_power_dbm = previous;
        apply_power();
        return false;
    }
    return true;
}

void wifi_tx::on_tx_done(const esp_80211_tx_info_t* info)
{
    wifi_tx& tx = wifi::instance().tx();
    if (!tx.release_driver_slot())
    {
        return;
    }
    if (info != nullptr && info->tx_status == WIFI_SEND_SUCCESS)
    {
        tx.air_pkt_.fetch_add(1, std::memory_order_relaxed);
    }
    else
    {
        tx.dropped_wifi_.fetch_add(1, std::memory_order_relaxed);
    }
}

bool wifi_tx::apply_tx_done_cb()
{
    const esp_err_t err = esp_wifi_register_80211_tx_cb(on_tx_done);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "register_80211_tx_cb failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}
