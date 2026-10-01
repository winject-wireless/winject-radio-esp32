#ifndef WINJECT_FRAME_H_
#define WINJECT_FRAME_H_

#include <stddef.h>
#include <stdint.h>

enum WinjectMode
{
    // Network + m-plane + WiFi d-plane (reset mode=WINJECT).
    WINJECT_MODE_STANDALONE = 1,
    // Boot-only: network + m-plane + HTTP OTA; no WiFi / d-plane endpoints.
    WINJECT_MODE_OTA = 2,
};

// Reads the STA MAC; call once after esp_wifi_init, before RX/TX tasks start.
bool frameBegin();
bool frameSetMode(WinjectMode mode);
WinjectMode frameGetMode();
const char* frameModeName(WinjectMode mode);

void frameGetStaMac(uint8_t mac[6]);
// Addr3 starts with WIFI_BSSID_PREFIX_STANDALONE.
bool frameAddr3PrefixMatch(const uint8_t* mpdu, size_t len);

#endif  // WINJECT_FRAME_H_
