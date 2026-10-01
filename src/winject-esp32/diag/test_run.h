#ifndef WINJECT_DIAG_TEST_RUN_H_
#define WINJECT_DIAG_TEST_RUN_H_

#include "mplane_backend.h"

#include <atomic>
#include <optional>
#include <stddef.h>
#include <stdint.h>

// Start/stop bookkeeping for one background TX test (test_ether_tx /
// test_wifi_tx). begin()/stop() run on the console task; the worker polls
// stop_requested() and calls finish() when it exits.
class test_run
{
public:
    // ok: caller must start the worker. already: a run is active. stale: id
    // repeats the last accepted start (duplicate / replayed command).
    mplane_status begin(std::optional<uint8_t> id);
    // Undo begin() when the worker could not be started; the id stays usable.
    void abort_begin();
    // ok when idle or the request matches the running id (or has none).
    mplane_status stop(std::optional<uint8_t> id);

    bool running() const;
    bool stop_requested() const;
    void finish();

private:
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::optional<uint8_t> last_id_;
    std::optional<uint8_t> prev_last_id_;
    std::optional<uint8_t> run_id_;
};

// Gap between frames of `bytes` to hold rate_kbps; 0 = unpaced.
uint32_t test_frame_interval_us(size_t bytes, uint32_t rate_kbps);

#endif  // WINJECT_DIAG_TEST_RUN_H_
