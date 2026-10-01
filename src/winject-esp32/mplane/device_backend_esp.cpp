#include "device_backend_esp.h"

#include "manager.h"
#include "settings.h"

#include "esp_log.h"
#include "esp_system.h"

static const char* TAG = "device_mplane";

// Long enough for the console to flush the reply datagram.
static constexpr uint64_t k_restart_delay_us = 200 * 1000;

device_backend_esp::device_backend_esp(manager& netmgr, settings& store,
                                       mplane_radio_backend* radio)
    : netmgr_(netmgr),
      store_(store),
      radio_(radio),
      tune_(store.boot().tune),
      offline_radio_(store.boot().radio),
      offline_filter_(store.boot().rx_filter)
{
}

device_backend_esp::~device_backend_esp()
{
    if (restart_timer_ != nullptr)
    {
        esp_timer_stop(restart_timer_);
        esp_timer_delete(restart_timer_);
    }
}

void device_backend_esp::restart_cb(void* arg)
{
    (void)arg;
    ESP_LOGI(TAG, "restarting");
    esp_restart();
}

mplane_status device_backend_esp::accept_reset_id(uint8_t id)
{
    if (store_.reset_id_is_duplicate(id))
    {
        return mplane_status::already;
    }
    if (!store_.store_reset_id(id))
    {
        return mplane_status::io_error;
    }
    return mplane_status::ok;
}

mplane_status device_backend_esp::restart(std::optional<WinjectMode> mode)
{
    if (mode && !store_.set_boot_mode(*mode))
    {
        return mplane_status::io_error;
    }
    if (restart_timer_ == nullptr)
    {
        esp_timer_create_args_t args = {};
        args.callback = restart_cb;
        args.name = "mplane_reset";
        const esp_err_t err = esp_timer_create(&args, &restart_timer_);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "restart timer create failed: %s",
                     esp_err_to_name(err));
            restart_timer_ = nullptr;
            return mplane_status::io_error;
        }
    }
    esp_timer_stop(restart_timer_);
    const esp_err_t err = esp_timer_start_once(restart_timer_, k_restart_delay_us);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "restart timer start failed: %s", esp_err_to_name(err));
        return mplane_status::io_error;
    }
    return mplane_status::ok;
}

network_config device_backend_esp::network() const
{
    return netmgr_.network();
}

mplane_status device_backend_esp::set_network(const network_config& cfg)
{
    if (!network_config_valid(cfg))
    {
        return mplane_status::invalid;
    }
    return netmgr_.set_network(cfg) ? mplane_status::ok
                                    : mplane_status::io_error;
}

tune_config device_backend_esp::tune() const
{
    return tune_;
}

mplane_status device_backend_esp::set_tune(const tune_config& cfg)
{
    if (!tune_config_valid(cfg, settings::tune_build_limits()))
    {
        return mplane_status::invalid;
    }
    tune_ = cfg;
    return mplane_status::ok;
}

mplane_status device_backend_esp::save(uint8_t slot)
{
    settings_snapshot snap;
    snap.network = netmgr_.network();
    snap.tune = tune_;
    snap.radio = radio_ != nullptr ? radio_->radio() : offline_radio_;
    snap.rx_filter =
        radio_ != nullptr ? radio_->rx_filter_addr3() : offline_filter_;
    return store_.save_slot(slot, snap) ? mplane_status::ok
                                        : mplane_status::io_error;
}

mplane_status device_backend_esp::apply_radio(mplane_radio_backend& radio,
                                              const radio_config& cfg,
                                              const mac_filter& rx_filter)
{
    radio_patch patch;
    patch.channel = cfg.channel;
    patch.tx_power_dbm = cfg.tx_power_dbm;
    patch.modulation = cfg.modulation;
    patch.cca_enabled = cfg.cca_enabled;
    const mplane_status st = radio.set_radio(patch);
    if (st != mplane_status::ok)
    {
        return st;
    }
    return radio.set_rx_filter_addr3(rx_filter);
}

mplane_status device_backend_esp::load(uint8_t slot)
{
    settings_snapshot snap;
    switch (store_.read_slot(slot, &snap))
    {
        case settings::read_result::ok:
            break;
        case settings::read_result::missing:
            return mplane_status::not_found;
        case settings::read_result::invalid:
        case settings::read_result::io_error:
            return mplane_status::io_error;
    }
    if (radio_ == nullptr)
    {
        offline_radio_ = snap.radio;
        offline_filter_ = snap.rx_filter;
    }
    else
    {
        const mplane_status st = apply_radio(*radio_, snap.radio, snap.rx_filter);
        if (st != mplane_status::ok)
        {
            ESP_LOGE(TAG, "load slot %u: radio apply failed", slot);
            return st;
        }
    }
    if (!store_.set_current_slot(slot))
    {
        return mplane_status::io_error;
    }
    tune_ = snap.tune;
    const network_config net = snap.network;
    if (!netmgr_.reactor().wake_up(
            [this, net]()
            {
                if (!netmgr_.set_network(net))
                {
                    ESP_LOGE(TAG, "load: deferred network apply failed");
                }
            }))
    {
        ESP_LOGW(TAG, "load slot %u: wake_up failed; applying network now", slot);
        if (!netmgr_.set_network(net))
        {
            ESP_LOGE(TAG, "load slot %u: network apply failed", slot);
        }
    }
    ESP_LOGI(TAG, "loaded slot %u", slot);
    return mplane_status::ok;
}
