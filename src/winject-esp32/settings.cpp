#include "settings.h"

#include "wifi.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char* TAG = "settings";
static const char* k_nvs_ns = "winject";
static const char* k_current_key = "cur";
static const char* k_mode_key = "mode";

namespace
{
// RAII nvs_handle_t.
class nvs_session
{
public:
    explicit nvs_session(bool write)
    {
        const esp_err_t err =
            nvs_open(k_nvs_ns, write ? NVS_READWRITE : NVS_READONLY, &handle_);
        if (err != ESP_OK)
        {
            // Read-only open of a never-written namespace is expected.
            if (write || err != ESP_ERR_NVS_NOT_FOUND)
            {
                ESP_LOGE(TAG, "nvs_open(%s, %s) failed: %s", k_nvs_ns,
                         write ? "rw" : "ro", esp_err_to_name(err));
            }
            handle_ = 0;
        }
    }

    ~nvs_session()
    {
        if (handle_ != 0)
        {
            nvs_close(handle_);
        }
    }

    nvs_session(const nvs_session&) = delete;
    nvs_session& operator=(const nvs_session&) = delete;

    bool ok() const
    {
        return handle_ != 0;
    }

    nvs_handle_t get() const
    {
        return handle_;
    }

private:
    nvs_handle_t handle_ = 0;
};

bool slot_ok(uint8_t slot)
{
    return slot < SETTINGS_SLOT_COUNT;
}

void slot_key(uint8_t slot, char* out, size_t out_len)
{
    snprintf(out, out_len, "s%u", static_cast<unsigned>(slot));
}

bool commit(const nvs_session& nvs, const char* what)
{
    const esp_err_t err = nvs_commit(nvs.get());
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs commit (%s) failed: %s", what, esp_err_to_name(err));
        return false;
    }
    return true;
}
}  // namespace

settings& settings::instance()
{
    static settings inst;
    return inst;
}

settings::settings() : boot_(defaults()) {}

tune_limits settings::tune_build_limits()
{
    tune_limits l;
    l.eth_rx_ring_sz = CONFIG_ETH_DMA_RX_BUFFER_NUM;
    l.eth_tx_ring_sz = CONFIG_ETH_DMA_TX_BUFFER_NUM;
    l.wifi_rx_ring_min = CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM;
    return l;
}

settings_snapshot settings::defaults()
{
    settings_snapshot s;
    esp_ip4_addr_t addr = {};
    esp_netif_set_ip4_addr(&addr, ETH_FALLBACK_ADDR);
    s.network.ip = addr.addr;
    s.tune.eth_rx_ring_sz = CONFIG_ETH_DMA_RX_BUFFER_NUM;
    s.tune.eth_tx_ring_sz = CONFIG_ETH_DMA_TX_BUFFER_NUM;
    s.tune.wifi_tx_ring_sz = CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM;
    s.tune.wifi_rx_ring_sz = CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM;
    return s;
}

bool settings::snapshot_valid(const settings_snapshot& snap)
{
    const radio_config& r = snap.radio;
    return network_config_valid(snap.network) &&
           tune_config_valid(snap.tune, tune_build_limits()) &&
           r.channel >= WIFI_CHANNEL_MIN && r.channel <= WIFI_CHANNEL_MAX &&
           r.tx_power_dbm >= WIFI_TX_POWER_DBM_MIN &&
           r.tx_power_dbm <= WIFI_TX_POWER_DBM_MAX &&
           wifi::modulation_supported(r.modulation, r.channel);
}

void settings::load_boot()
{
    boot_ = defaults();
    boot_mode_ = WINJECT_MODE_STANDALONE;
    current_slot_ = 0;
    {
        const nvs_session nvs(false);
        if (nvs.ok())
        {
            uint8_t v = 0;
            if (nvs_get_u8(nvs.get(), k_current_key, &v) == ESP_OK && slot_ok(v))
            {
                current_slot_ = v;
            }
            if (nvs_get_u8(nvs.get(), k_mode_key, &v) == ESP_OK)
            {
                if (v == WINJECT_MODE_STANDALONE || v == WINJECT_MODE_OTA)
                {
                    boot_mode_ = static_cast<WinjectMode>(v);
                }
                else
                {
                    ESP_LOGW(TAG, "ignoring stored mode %u", v);
                }
            }
        }
    }

    settings_snapshot snap;
    switch (read_slot(current_slot_, &snap))
    {
        case read_result::ok:
            boot_ = snap;
            ESP_LOGI(TAG, "boot mode %s slot %u", frameModeName(boot_mode_),
                     current_slot_);
            break;
        case read_result::missing:
            ESP_LOGI(TAG, "boot mode %s slot %u empty, using defaults",
                     frameModeName(boot_mode_), current_slot_);
            break;
        case read_result::invalid:
        case read_result::io_error:
            ESP_LOGW(TAG, "boot mode %s slot %u unreadable, using defaults",
                     frameModeName(boot_mode_), current_slot_);
            break;
    }
}

WinjectMode settings::boot_mode() const
{
    return boot_mode_;
}

const settings_snapshot& settings::boot() const
{
    return boot_;
}

uint8_t settings::current_slot() const
{
    return current_slot_;
}

bool settings::set_boot_mode(WinjectMode mode)
{
    if (mode != WINJECT_MODE_STANDALONE && mode != WINJECT_MODE_OTA)
    {
        return false;
    }
    const nvs_session nvs(true);
    if (!nvs.ok())
    {
        return false;
    }
    const esp_err_t err =
        nvs_set_u8(nvs.get(), k_mode_key, static_cast<uint8_t>(mode));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "set mode %s failed: %s", frameModeName(mode),
                 esp_err_to_name(err));
        return false;
    }
    if (!commit(nvs, "mode"))
    {
        return false;
    }
    ESP_LOGI(TAG, "boot mode -> %s", frameModeName(mode));
    return true;
}

bool settings::save_slot(uint8_t slot, const settings_snapshot& snap)
{
    if (!slot_ok(slot))
    {
        return false;
    }
    uint8_t blob[k_settings_blob_size];
    const size_t len = settings_blob_pack(snap, blob, sizeof(blob));
    if (len == 0)
    {
        ESP_LOGE(TAG, "slot %u pack failed", slot);
        return false;
    }
    char key[8];
    slot_key(slot, key, sizeof(key));
    const nvs_session nvs(true);
    if (!nvs.ok())
    {
        return false;
    }
    esp_err_t err = nvs_set_blob(nvs.get(), key, blob, len);
    if (err == ESP_OK)
    {
        err = nvs_set_u8(nvs.get(), k_current_key, slot);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "save slot %u failed: %s", slot, esp_err_to_name(err));
        return false;
    }
    if (!commit(nvs, key))
    {
        return false;
    }
    current_slot_ = slot;
    ESP_LOGI(TAG, "saved slot %u", slot);
    return true;
}

settings::read_result settings::read_slot(uint8_t slot, settings_snapshot* out)
{
    if (!slot_ok(slot) || out == nullptr)
    {
        return read_result::invalid;
    }
    char key[8];
    slot_key(slot, key, sizeof(key));
    const nvs_session nvs(false);
    if (!nvs.ok())
    {
        return read_result::missing;
    }
    // Large enough for legacy winject-esp32 blobs; only boot/console call read_slot
    // and never concurrently.
    static uint8_t blob[k_settings_blob_legacy_max_size];
    size_t len = sizeof(blob);
    const esp_err_t err = nvs_get_blob(nvs.get(), key, blob, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        return read_result::missing;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH)
    {
        ESP_LOGW(TAG, "slot %u: blob larger than legacy max (%u bytes)", slot,
                 static_cast<unsigned>(sizeof(blob)));
        return read_result::invalid;
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "slot %u read failed: %s", slot, esp_err_to_name(err));
        return read_result::io_error;
    }
    if (len > 0 && blob[0] == k_settings_blob_version)
    {
        settings_snapshot snap;
        if (!settings_blob_unpack(blob, len, &snap) || !snapshot_valid(snap))
        {
            ESP_LOGW(TAG, "slot %u: invalid v%u blob (%u bytes)", slot,
                     k_settings_blob_version, static_cast<unsigned>(len));
            return read_result::invalid;
        }
        *out = snap;
        return read_result::ok;
    }
    if (len > 0 && blob[0] >= k_settings_blob_legacy_min &&
        blob[0] <= k_settings_blob_legacy_max)
    {
        settings_snapshot snap = defaults();
        if (!settings_blob_unpack_legacy(blob, len, &snap))
        {
            ESP_LOGW(TAG, "slot %u: corrupt legacy v%u blob (%u bytes)", slot,
                     blob[0], static_cast<unsigned>(len));
            return read_result::invalid;
        }
        if (!snapshot_valid(snap))
        {
            snap.radio = defaults().radio;
            if (!snapshot_valid(snap))
            {
                ESP_LOGW(TAG, "slot %u: invalid legacy v%u blob (%u bytes)",
                         slot, blob[0], static_cast<unsigned>(len));
                return read_result::invalid;
            }
        }
        ESP_LOGI(TAG, "slot %u: migrated legacy v%u blob", slot, blob[0]);
        *out = snap;
        return read_result::ok;
    }
    ESP_LOGW(TAG, "slot %u: invalid or pre-v%u blob (%u bytes, v%u)", slot,
             k_settings_blob_version, static_cast<unsigned>(len),
             len > 0 ? blob[0] : 0u);
    return read_result::invalid;
}

bool settings::set_current_slot(uint8_t slot)
{
    if (!slot_ok(slot))
    {
        return false;
    }
    const nvs_session nvs(true);
    if (!nvs.ok())
    {
        return false;
    }
    const esp_err_t err = nvs_set_u8(nvs.get(), k_current_key, slot);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "set current slot %u failed: %s", slot,
                 esp_err_to_name(err));
        return false;
    }
    if (!commit(nvs, "cur"))
    {
        return false;
    }
    current_slot_ = slot;
    return true;
}
