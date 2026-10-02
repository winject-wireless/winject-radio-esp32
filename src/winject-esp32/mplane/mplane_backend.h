#ifndef WINJECT_MPLANE_MPLANE_BACKEND_H_
#define WINJECT_MPLANE_MPLANE_BACKEND_H_

#include "config_types.h"
#include "frame.h"

#include <array>
#include <optional>
#include <stdint.h>

// Device-side operations behind the m-plane commands. The firmware and the
// host simulator each implement these; mplane_commands owns parsing and reply
// formatting. Calls arrive on the m-plane task and must not block for long.

enum class mplane_status : uint8_t
{
    ok,
    invalid,      // EINVAL
    already,      // EALREADY: a test is in progress
    stale,        // ESTALE: request id is stale / does not match
    no_device,    // ENODEV: radio down or OTA mode
    io_error,     // EIO: NVS / driver failure
    not_found,    // ENOENT: empty settings slot
    unsupported,  // ENOTSUP
};

// POSIX-style token used in `NOK <code>` replies.
const char* mplane_status_code(mplane_status status);

using mac_address = std::array<uint8_t, 6>;

// Unset fields keep their current value.
struct radio_patch
{
    std::optional<uint8_t> channel;
    std::optional<int8_t> tx_power_dbm;
    const char* modulation = nullptr;
    std::optional<bool> cca_enabled;
};

struct rx_test_stats
{
    uint64_t pkt = 0;
    uint64_t byt = 0;
    uint64_t fec_error_pkt = 0;
};

enum class fcs_mode : uint8_t
{
    signal,
    actual,
};

struct radio_caps
{
    fcs_mode fcs = fcs_mode::actual;
};

// count == 0 stops the run identified by id. rate_kbps == 0 = unpaced.
struct ether_tx_request
{
    uint8_t id = 0;
    uint32_t host = 0;
    uint16_t port = 0;
    uint16_t mtu = 0;
    uint16_t count = 0;
    uint32_t rate_kbps = 0;
};

// All filters disabled = leave test mode.
struct wifi_rx_match
{
    mac_filter addr1;
    mac_filter addr2;
    mac_filter addr3;

    bool active() const
    {
        return addr1.enabled || addr2.enabled || addr3.enabled;
    }
};

// mtu = full MPDU length (24-byte header + payload). Unset addresses default
// to broadcast (addr1), the radio's own MAC (addr2), and zero (addr3).
// count == 0 stops the run.
struct wifi_tx_request
{
    std::optional<uint8_t> id;
    std::optional<mac_address> addr1;
    std::optional<mac_address> addr2;
    std::optional<mac_address> addr3;
    uint16_t mtu = 0;
    uint16_t count = 0;
    uint32_t rate_kbps = 0;
};

class mplane_device_backend
{
public:
    virtual ~mplane_device_backend() = default;

    // Persists `mode` when given, then restarts after the reply is flushed.
    virtual mplane_status restart(std::optional<WinjectMode> mode) = 0;

    // `reset id=<u8>` idempotency (persisted on device).
    virtual mplane_status accept_reset_id(uint8_t id) = 0;

    virtual network_config network() const = 0;
    virtual mplane_status set_network(const network_config& cfg) = 0;

    // Configured (not yet booted) values; see tune_config.
    virtual tune_config tune() const = 0;
    virtual mplane_status set_tune(const tune_config& cfg) = 0;

    virtual mplane_status save(uint8_t slot) = 0;
    virtual mplane_status load(uint8_t slot) = 0;

    virtual int64_t uptime_us() const = 0;
};

class mplane_radio_backend
{
public:
    virtual ~mplane_radio_backend() = default;

    // Current occupancy, not configured capacity.
    virtual uint8_t tx_queue_size() const = 0;
    virtual uint8_t tx_in_flight() const = 0;
    virtual uint8_t rx_queue_size() const = 0;

    virtual uint32_t tx_dropped_invalid_frame() const = 0;
    virtual uint32_t tx_dropped_tx_queue() const = 0;
    virtual uint32_t tx_dropped_wifi() const = 0;
    virtual uint32_t rx_dropped_filter_mismatched() const = 0;
    virtual uint32_t rx_dropped_rx_queue() const = 0;
    virtual uint32_t rx_dropped_no_peer() const = 0;
    virtual uint32_t rx_dropped_send_failed() const = 0;
    virtual uint32_t tx_ether_pkt() const = 0;
    virtual uint32_t rx_ether_pkt() const = 0;
    virtual uint32_t tx_air_pkt() const = 0;
    virtual uint32_t rx_air_pkt() const = 0;

    virtual radio_config radio() const = 0;
    virtual radio_caps caps() const = 0;
    // false until a frame has been received.
    virtual bool rx_rssi(int8_t* dbm) const = 0;
    virtual mplane_status set_radio(const radio_patch& patch) = 0;

    virtual mac_filter rx_filter_addr3() const = 0;
    virtual mplane_status set_rx_filter_addr3(const mac_filter& filter) = 0;

    // Space-separated modulation names for `help`.
    virtual const char* modulation_list() const = 0;
};

class mplane_test_backend
{
public:
    virtual ~mplane_test_backend() = default;

    // port == 0 closes the test socket.
    virtual mplane_status set_ether_rx_port(uint16_t port) = 0;
    virtual rx_test_stats ether_rx_stats(bool clear) = 0;
    virtual mplane_status ether_tx(const ether_tx_request& req) = 0;

    virtual mplane_status set_wifi_rx_match(const wifi_rx_match& match) = 0;
    virtual rx_test_stats wifi_rx_stats(bool clear) = 0;
    virtual mplane_status wifi_tx(const wifi_tx_request& req) = 0;
};

#endif  // WINJECT_MPLANE_MPLANE_BACKEND_H_
