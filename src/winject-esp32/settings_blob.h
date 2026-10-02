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

// Versioned little-endian wire format for settings_snapshot.
inline constexpr uint8_t k_settings_blob_version = 9;
inline constexpr size_t k_settings_blob_size = 1 + 8 + 3 + SETTINGS_MODULATION_MAX + 7 + 7;

// Oldest / newest winject-esp32 (pre-radio) slot blob versions understood by
// settings_blob_unpack_legacy.
inline constexpr uint8_t k_settings_blob_legacy_min = 3;
inline constexpr uint8_t k_settings_blob_legacy_max = 7;
// Bytes of the legacy prefix the decoder reads (through network_mode).
inline constexpr size_t k_settings_blob_legacy_prefix = 28;
// Largest legacy blob read_slot must be able to fetch (old k_blob_max).
inline constexpr size_t k_settings_blob_legacy_max_size = 2600;

// Returns the encoded length, or 0 if cap < k_settings_blob_size.
size_t settings_blob_pack(const settings_snapshot& snap, uint8_t* buf,
                          size_t cap);
// Structural decode only (version, length, enums, NUL-terminated modulation);
// semantic range checks are the caller's job.
bool settings_blob_unpack(const uint8_t* buf, size_t len,
                          settings_snapshot* out);

// Decodes the network + radio fields of a winject-esp32 v3..v7 slot blob onto
// *inout (caller pre-fills defaults); all other fields are left untouched.
// Structural decode only, like settings_blob_unpack.
bool settings_blob_unpack_legacy(const uint8_t* buf, size_t len,
                                 settings_snapshot* inout);

#endif  // WINJECT_SETTINGS_BLOB_H_
