#include "frame.h"

#include "config.h"

#include <atomic>
#include <string.h>

#ifdef WINJECT_HOST_TEST
#include "host_idf_stub.h"
#else
#include "esp_log.h"
#include "esp_wifi.h"
#endif

static const char* TAG = "frame";
static const uint8_t kPrefixStandalone[4] = {WIFI_BSSID_PREFIX_STANDALONE};

static std::atomic<WinjectMode> g_mode{WINJECT_MODE_STANDALONE};
// Written once by frameBegin() before readers exist.
static uint8_t g_staMac[6];

bool frameAddr3PrefixMatch(const uint8_t* mpdu, size_t len)
{
    if (mpdu == nullptr || len < WIFI_HDR_LEN)
    {
        return false;
    }
    return memcmp(mpdu + 16, kPrefixStandalone, sizeof(kPrefixStandalone)) ==
           0;
}

const char* frameModeName(WinjectMode mode)
{
    switch (mode)
    {
        case WINJECT_MODE_STANDALONE:
            return "WINJECT";
        case WINJECT_MODE_OTA:
            return "OTA";
        default:
            return "UNKNOWN";
    }
}

bool frameSetMode(WinjectMode mode)
{
    if (mode != WINJECT_MODE_STANDALONE && mode != WINJECT_MODE_OTA)
    {
        return false;
    }
    g_mode.store(mode, std::memory_order_relaxed);
    ESP_LOGI(TAG, "mode %s", frameModeName(mode));
    return true;
}

WinjectMode frameGetMode()
{
    return g_mode.load(std::memory_order_relaxed);
}

bool frameBegin()
{
    uint8_t mac[6] = {};
    const esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "frame sta mac failed: %s", esp_err_to_name(err));
        return false;
    }
    memcpy(g_staMac, mac, sizeof(g_staMac));
    ESP_LOGI(TAG, "STA %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    return true;
}

void frameGetStaMac(uint8_t mac[6])
{
    if (mac == nullptr)
    {
        return;
    }
    memcpy(mac, g_staMac, sizeof(g_staMac));
}
