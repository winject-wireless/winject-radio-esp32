#include "config.h"
#include "console.h"
#include "device_backend_esp.h"
#include "frame.h"
#include "manager.h"
#include "mplane_commands.h"
#include "ota.h"
#include "packet.h"
#include "radio_backend_esp.h"
#include "settings.h"
#include "test_backend_esp.h"
#include "upstream_rx_endpoint.h"
#include "upstream_tx_endpoint.h"
#include "wifi.h"

#include <optional>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char* TAG = "winject";

// M-plane objects live for the whole program (console callbacks use them).
static console g_console;
static std::optional<radio_backend_esp> g_radio_backend;
static std::optional<test_backend_esp> g_test_backend;
static std::optional<device_backend_esp> g_device_backend;
static std::optional<mplane_commands> g_commands;

// Never abort on optional bring-up: a dead component must leave Ethernet +
// HTTP OTA (+ console) alive for rescue.
static void idle_forever()
{
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void start_console(manager& netmgr, mplane_radio_backend* radio,
                          mplane_test_backend* test)
{
    g_device_backend.emplace(netmgr, settings::instance(), radio);
    g_commands.emplace(*g_device_backend, radio, test);
    if (!g_console.init(netmgr, *g_commands))
    {
        ESP_LOGE(TAG, "console init failed");
    }
}

// Sizes queues / pools / driver rings from the boot tune and brings up the
// radio and d-plane. false = radio unusable (console still starts).
static bool start_radio(const settings_snapshot& boot)
{
    const tune_config& tune = boot.tune;
    wifi& radio = wifi::instance();
    if (!packet_allocator::rx().init(tune.rx_queue_sz) ||
        !radio.tx().init(tune.tx_queue_sz) || !radio.rx().init(tune.rx_queue_sz))
    {
        ESP_LOGE(TAG, "radio queue init failed (tx=%u rx=%u)",
                 static_cast<unsigned>(tune.tx_queue_sz),
                 static_cast<unsigned>(tune.rx_queue_sz));
        return false;
    }
    if (!radio.initialize(tune.wifi_tx_ring_sz, tune.wifi_rx_ring_sz))
    {
        ESP_LOGE(TAG, "wifi radio init failed");
        return false;
    }
    if (!frameBegin())
    {
        ESP_LOGE(TAG, "802.11 frame init failed");
        return false;
    }

    g_radio_backend.emplace(radio);
    if (device_backend_esp::apply_radio(*g_radio_backend, boot.radio,
                                        boot.rx_filter) != mplane_status::ok)
    {
        ESP_LOGE(TAG, "boot radio config apply failed (channel=%u %s)",
                 static_cast<unsigned>(boot.radio.channel),
                 boot.radio.modulation);
    }

    if (!radio.tx().start())
    {
        return false;
    }
    upstream_tx_endpoint::instance().set_sink(radio.tx());
    if (!upstream_rx_endpoint::instance().start(radio.rx()))
    {
        ESP_LOGE(TAG, "d-plane rx start failed");
    }
    return true;
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "winject-esp32 starting  reset %d", (int)esp_reset_reason());

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        err = nvs_flash_erase();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "nvs erase failed: %s", esp_err_to_name(err));
        }
        else
        {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs init failed: %s (continuing)", esp_err_to_name(err));
    }

    if (esp_netif_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_netif_init failed — cannot start network/OTA");
        idle_forever();
    }
    if (esp_event_loop_create_default() != ESP_OK)
    {
        ESP_LOGE(TAG, "event loop failed — cannot start network/OTA");
        idle_forever();
    }

    settings& store = settings::instance();
    store.load_boot();
    const settings_snapshot& boot = store.boot();

    manager& netmgr = manager::instance();
    if (!netmgr.set_network(boot.network))
    {
        ESP_LOGE(TAG, "boot network config rejected");
    }
    if (!netmgr.start(boot.tune.eth_dma_burst_len))
    {
        ESP_LOGE(TAG, "network manager start failed");
    }

    // HTTP OTA before WiFi so radio init failure still leaves rescue.
    otaBegin(netmgr);

    frameSetMode(store.boot_mode());
    if (store.boot_mode() == WINJECT_MODE_OTA)
    {
        ESP_LOGI(TAG, "OTA mode: network + m-plane + HTTP update only");
        start_console(netmgr, nullptr, nullptr);
        return;
    }

    const bool radio_ok = start_radio(boot);
    g_test_backend.emplace(wifi::instance());
    start_console(netmgr, radio_ok ? &*g_radio_backend : nullptr,
                  &*g_test_backend);
    if (!radio_ok)
    {
        ESP_LOGW(TAG, "running degraded: OTA/m-plane up, radio down");
    }
}
