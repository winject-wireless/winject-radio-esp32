#ifndef WINJECT_WIFI_H_
#define WINJECT_WIFI_H_

#include "config.h"
#include "config_types.h"
#include "indicator_led.h"
#include "wifi_rx.h"
#include "wifi_tx.h"

#include <atomic>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "bfc-esp32/semaphore.hpp"

// ESP32 radio in STA promiscuous mode for raw inject + monitor. Setters may be
// called from any task; they serialize on an internal lock.
class wifi
{
    friend class wifi_tx;
    friend class wifi_rx;

public:
    static wifi& instance();
    wifi(const wifi&) = delete;
    wifi& operator=(const wifi&) = delete;

    // tx_ring / rx_ring: driver dynamic buffer counts (tune wifi_tx_ring_sz /
    // wifi_rx_ring_sz). tx().init() and rx().init() must have run.
    bool initialize(uint8_t tx_ring, uint8_t rx_ring);
    bool ready() const;
    // Channel 14 requires a DSSS/CCK modulation; callers changing both must
    // order the calls so every intermediate state is valid.
    bool set_channel(uint8_t channel);
    bool set_modulation(const char* name);
    bool set_cca_enabled(bool enabled);
    bool set_tx_power(int8_t dbm);
    bool config(radio_config* out) const;
    // Known modulation name that is legal on channel (14 is DSSS/CCK only).
    static bool modulation_supported(const char* name, uint8_t channel);
    static const char* modulation_list();

    wifi_tx& tx();
    wifi_rx& rx();

private:
    wifi();

    void pulse_tx_led();
    void pulse_rx_led();
    wifi_phy_rate_t phy_rate() const
    {
        return modulation_rate;
    }
    esp_err_t apply_country();
    // Re-apply STA protocol after TX rate / promisc (IDF may clamp to 11b|11g).
    bool apply_sta_decode_protocol();
    void init_activity_leds();
    bool apply_channel();
    bool apply_modulation();

    wifi_tx tx_;
    wifi_rx rx_;
    mutable bfc::semaphore lock;
    uint8_t channel = WIFI_DEFAULT_CHANNEL;
    const char* modulation_name = WIFI_DEFAULT_MODULATION;
    wifi_phy_rate_t modulation_rate = WIFI_PHY_RATE_1M_L;
    std::atomic<bool> ready_{false};
    indicator_led rx_led;
    indicator_led tx_led;
};

#endif  // WINJECT_WIFI_H_
