#ifndef WINJECT_SETTINGS_BLOB_H_
#define WINJECT_SETTINGS_BLOB_H_

#include "config_types.h"

#include <stddef.h>
#include <stdint.h>

// Contents of one `save <slot>` NVS blob.
struct settings_snapshot
{
    network_config network;
    radio_config radio;
    mac_filter rx_filter;
    tune_config tune;
};

// Versioned little-endian wire format for settings_snapshot. Slots written by
// older firmware (version < k_settings_blob_version) are rejected.
inline constexpr uint8_t k_settings_blob_version = 9;
inline constexpr size_t k_settings_blob_size = 1 + 8 + 3 + SETTINGS_MODULATION_MAX + 7 + 7;

// Returns the encoded length, or 0 if cap < k_settings_blob_size.
size_t settings_blob_pack(const settings_snapshot& snap, uint8_t* buf,
                          size_t cap);
// Structural decode only (version, length, enums, NUL-terminated modulation);
// semantic range checks are the caller's job.
bool settings_blob_unpack(const uint8_t* buf, size_t len,
                          settings_snapshot* out);

#endif  // WINJECT_SETTINGS_BLOB_H_
