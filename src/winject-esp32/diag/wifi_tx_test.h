#ifndef WINJECT_DIAG_WIFI_TX_TEST_H_
#define WINJECT_DIAG_WIFI_TX_TEST_H_

#include "mplane_backend.h"
#include "test_run.h"

#include <stdint.h>

class wifi;

// test_wifi_tx: paced generator of 802.11 data frames through wifi_tx (same
// queue as the d-plane). Public methods run on the console task.
class wifi_tx_test
{
public:
    explicit wifi_tx_test(wifi& radio);
    wifi_tx_test(const wifi_tx_test&) = delete;
    wifi_tx_test& operator=(const wifi_tx_test&) = delete;

    // count=0 stops the running generator. Unset addresses default to
    // addr1=broadcast, addr2=own STA MAC, addr3=00:00:00:00:00:00.
    mplane_status run(const wifi_tx_request& req);

private:
    static void task(void* arg);
    void run_task();

    wifi& radio_;
    test_run run_;
    wifi_tx_request req_;
};

#endif  // WINJECT_DIAG_WIFI_TX_TEST_H_
