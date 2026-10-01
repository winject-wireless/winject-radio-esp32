#include "radio_backend_esp.h"

#include "wifi.h"

#include "esp_log.h"

static const char* TAG = "radio_mplane";

radio_backend_esp::radio_backend_esp(wifi& radio) : radio_(radio) {}

uint8_t radio_backend_esp::tx_queue_size() const
{
    return radio_.tx().queue_size();
}

uint8_t radio_backend_esp::tx_in_flight() const
{
    return radio_.tx().in_flight();
}

uint8_t radio_backend_esp::rx_queue_size() const
{
    return radio_.rx().queue_size();
}

radio_config radio_backend_esp::radio() const
{
    radio_config out{};
    if (!radio_.config(&out))
    {
        ESP_LOGW(TAG, "radio(): config lock failed");
    }
    return out;
}

bool radio_backend_esp::rx_rssi(int8_t* dbm) const
{
    return radio_.rx().last_rssi(dbm);
}

mplane_status radio_backend_esp::set_radio(const radio_patch& patch)
{
    if (!radio_.ready())
    {
        return mplane_status::no_device;
    }
    radio_config cur{};
    if (!radio_.config(&cur))
    {
        return mplane_status::io_error;
    }
    const uint8_t channel = patch.channel.value_or(cur.channel);
    const char* modulation =
        patch.modulation != nullptr ? patch.modulation : cur.modulation;
    if (!wifi::modulation_supported(modulation, channel))
    {
        return mplane_status::invalid;
    }

    // Channel 14 is 802.11b-only: switch to DSSS/CCK before entering it, and
    // leave it before switching to OFDM.
    bool ok = true;
    if (channel == 14)
    {
        ok = (patch.modulation == nullptr || radio_.set_modulation(modulation)) &&
             (!patch.channel || radio_.set_channel(channel));
        if (!ok)
        {
            if (patch.modulation != nullptr)
            {
                radio_.set_modulation(cur.modulation);
            }
            if (patch.channel)
            {
                radio_.set_channel(cur.channel);
            }
        }
    }
    else
    {
        ok = (!patch.channel || radio_.set_channel(channel)) &&
             (patch.modulation == nullptr || radio_.set_modulation(modulation));
        if (!ok)
        {
            if (patch.modulation != nullptr)
            {
                radio_.set_modulation(cur.modulation);
            }
            if (patch.channel)
            {
                radio_.set_channel(cur.channel);
            }
        }
    }
    if (ok && patch.cca_enabled)
    {
        ok = radio_.set_cca_enabled(*patch.cca_enabled);
    }
    if (ok && patch.tx_power_dbm)
    {
        ok = radio_.set_tx_power(*patch.tx_power_dbm);
    }
    if (!ok)
    {
        ESP_LOGE(TAG, "radio_tx apply failed (channel=%u modulation=%s)",
                 static_cast<unsigned>(channel), modulation);
        return mplane_status::io_error;
    }
    return mplane_status::ok;
}

mac_filter radio_backend_esp::rx_filter_addr3() const
{
    return radio_.rx().filter_addr3();
}

mplane_status radio_backend_esp::set_rx_filter_addr3(const mac_filter& filter)
{
    radio_.rx().set_filter_addr3(filter);
    return mplane_status::ok;
}

const char* radio_backend_esp::modulation_list() const
{
    return wifi::modulation_list();
}
