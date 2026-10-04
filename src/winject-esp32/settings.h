#ifndef WINJECT_SETTINGS_H_
#define WINJECT_SETTINGS_H_

#include "config.h"
#include "frame.h"
#include "settings_blob.h"

#include <stdint.h>

// NVS store (namespace "winject"): boot mode, current slot, and the
// `save` / `load` slot blobs. Boot and console task only.
class settings
{
public:
    enum class read_result
    {
        ok,
        missing,
        invalid,
        io_error,
    };

    static settings& instance();
    settings(const settings&) = delete;
    settings& operator=(const settings&) = delete;

    // Limits / defaults derived from sdkconfig (EMAC + WiFi driver buffers).
    static tune_limits tune_build_limits();
    static settings_snapshot defaults();
    // Range checks a decoded snapshot (network, radio, tune).
    static bool snapshot_valid(const settings_snapshot& snap);

    // Reads the boot mode and the current slot; missing or invalid data falls
    // back to defaults (logged). Call once after nvs_flash_init.
    void load_boot();
    WinjectMode boot_mode() const;
    // Snapshot this boot was configured from (its tune is the effective one).
    const settings_snapshot& boot() const;
    uint8_t current_slot() const;

    bool set_boot_mode(WinjectMode mode);

    // Writes slot and makes it current, so it is applied at next boot.
    bool save_slot(uint8_t slot, const settings_snapshot& snap);
    read_result read_slot(uint8_t slot, settings_snapshot* out);
    bool set_current_slot(uint8_t slot);

private:
    settings();

    WinjectMode boot_mode_ = WINJECT_MODE_STANDALONE;
    uint8_t current_slot_ = 0;
    settings_snapshot boot_;
};

#endif  // WINJECT_SETTINGS_H_
