#include "fcs.h"

#include <string.h>

#ifndef WINJECT_HOST_TEST
#include "esp_rom_crc.h"
#endif

uint32_t wifi_fcs_compute(const uint8_t* mpdu, size_t len)
{
    if (mpdu == nullptr)
    {
        return 0;
    }
#ifdef WINJECT_HOST_TEST
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= mpdu[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
#else
    // The ROM routine inverts on entry and exit: init 0 == standard CRC-32.
    return esp_rom_crc32_le(0, mpdu, static_cast<uint32_t>(len));
#endif
}

bool wifi_fcs_matches(const uint8_t* frame, size_t len)
{
    if (frame == nullptr || len < 4)
    {
        return false;
    }
    const uint8_t* fcs = frame + len - 4;
    const uint32_t got = static_cast<uint32_t>(fcs[0]) |
                         (static_cast<uint32_t>(fcs[1]) << 8) |
                         (static_cast<uint32_t>(fcs[2]) << 16) |
                         (static_cast<uint32_t>(fcs[3]) << 24);
    return wifi_fcs_compute(frame, len - 4) == got;
}

void wifi_fcs_store(const uint8_t* mpdu, size_t len, uint8_t out[4])
{
    if (out == nullptr)
    {
        return;
    }
    const uint32_t crc = wifi_fcs_compute(mpdu, len);
    out[0] = static_cast<uint8_t>(crc);
    out[1] = static_cast<uint8_t>(crc >> 8);
    out[2] = static_cast<uint8_t>(crc >> 16);
    out[3] = static_cast<uint8_t>(crc >> 24);
}

void write_fcs_signal(uint8_t* trailer, uint8_t rx_state)
{
    if (trailer == nullptr)
    {
        return;
    }
    const uint32_t verdict = (rx_state == 0) ? 0u : 0xFFFFFFFFu;
    memcpy(trailer, &verdict, 4);
}
