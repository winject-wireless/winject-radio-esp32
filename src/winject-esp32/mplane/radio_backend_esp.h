#ifndef WINJECT_MPLANE_RADIO_BACKEND_ESP_H_
#define WINJECT_MPLANE_RADIO_BACKEND_ESP_H_

#include "mplane_backend.h"

class wifi;

// mplane_radio_backend over the live ESP32 radio (wifi / wifi_tx / wifi_rx).
class radio_backend_esp : public mplane_radio_backend
{
public:
    explicit radio_backend_esp(wifi& radio);

    uint8_t tx_queue_size() const override;
    uint8_t tx_in_flight() const override;
    uint8_t rx_queue_size() const override;

    radio_config radio() const override;
    bool rx_rssi(int8_t* dbm) const override;
    // Rejects unknown modulations and OFDM on channel 14 as invalid; applies
    // channel and modulation in the order that keeps each step legal.
    mplane_status set_radio(const radio_patch& patch) override;

    mac_filter rx_filter_addr3() const override;
    mplane_status set_rx_filter_addr3(const mac_filter& filter) override;

    const char* modulation_list() const override;

private:
    wifi& radio_;
};

#endif  // WINJECT_MPLANE_RADIO_BACKEND_ESP_H_
