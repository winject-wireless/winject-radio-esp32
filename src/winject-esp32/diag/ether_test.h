#ifndef WINJECT_DIAG_ETHER_TEST_H_
#define WINJECT_DIAG_ETHER_TEST_H_

#include "config.h"
#include "mplane_backend.h"
#include "test_run.h"

#include <atomic>
#include <stdint.h>

#include "bfc-esp32/semaphore.hpp"
#include "bfc-esp32/socket.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// test_ether_rx / test_ether_tx: UDP sink with packet/byte counters and a
// paced UDP generator. Public methods run on the console task.
class ether_test
{
public:
    ether_test() = default;
    ether_test(const ether_test&) = delete;
    ether_test& operator=(const ether_test&) = delete;

    // Rebinds the sink to port (0 closes it). Counters are kept.
    mplane_status set_rx_port(uint16_t port);
    rx_test_stats rx_stats(bool clear);
    // count=0 stops the running generator.
    mplane_status tx(const ether_tx_request& req);

private:
    static void rx_task(void* arg);
    void run_rx();
    static void tx_task(void* arg);
    void run_tx();

    // Guards rx_sock_ between the console (rebind) and the RX task (drain).
    bfc::semaphore rx_lock_;
    bfc::socket rx_sock_;
    TaskHandle_t rx_task_ = nullptr;
    std::atomic<uint64_t> rx_pkt_{0};
    std::atomic<uint64_t> rx_byt_{0};
    uint8_t rx_buf_[WIFI_RADIO_INJECT_MAX];

    test_run tx_run_;
    ether_tx_request tx_req_;
    uint8_t tx_buf_[ETHER_TEST_MTU_MAX];
};

#endif  // WINJECT_DIAG_ETHER_TEST_H_
