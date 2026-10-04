#ifndef WINJECT_MPLANE_DEVICE_BACKEND_ESP_H_
#define WINJECT_MPLANE_DEVICE_BACKEND_ESP_H_

#include "mplane_backend.h"

#include "esp_timer.h"

class manager;
class settings;

// mplane_device_backend for the firmware: network via manager, slots and boot
// mode via settings, deferred esp_restart. tune is held in RAM until `save`.
class device_backend_esp : public mplane_device_backend
{
public:
    // radio is nullptr in OTA / degraded boots; radio settings are then only
    // carried (boot snapshot, updated by load) into the next save.
    device_backend_esp(manager& netmgr, settings& store,
                       mplane_radio_backend* radio);
    ~device_backend_esp() override;

    device_backend_esp(const device_backend_esp&) = delete;
    device_backend_esp& operator=(const device_backend_esp&) = delete;

    mplane_status restart(std::optional<WinjectMode> mode) override;

    network_config network() const override;
    mplane_status set_network(const network_config& cfg) override;

    tune_config tune() const override;
    mplane_status set_tune(const tune_config& cfg) override;

    mplane_status save(uint8_t slot) override;
    mplane_status load(uint8_t slot) override;

    int64_t uptime_us() const override;
    const char* version() const override;

    // Applies a stored radio config + rx filter (boot and `load`).
    static mplane_status apply_radio(mplane_radio_backend& radio,
                                     const radio_config& cfg,
                                     const mac_filter& rx_filter);

private:
    static void restart_cb(void* arg);

    manager& netmgr_;
    settings& store_;
    mplane_radio_backend* radio_;
    tune_config tune_;
    radio_config offline_radio_;
    mac_filter offline_filter_;
    esp_timer_handle_t restart_timer_ = nullptr;
};

#endif  // WINJECT_MPLANE_DEVICE_BACKEND_ESP_H_
