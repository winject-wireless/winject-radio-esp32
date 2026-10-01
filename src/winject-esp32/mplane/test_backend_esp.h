#ifndef WINJECT_MPLANE_TEST_BACKEND_ESP_H_
#define WINJECT_MPLANE_TEST_BACKEND_ESP_H_

#include "ether_test.h"
#include "mplane_backend.h"
#include "wifi_tx_test.h"

class wifi;

// mplane_test_backend: Ethernet UDP sink/generator and WiFi test tap/generator.
// WiFi tests reply ENODEV while the radio is down.
class test_backend_esp : public mplane_test_backend
{
public:
    explicit test_backend_esp(wifi& radio);

    // Rejects the m-plane, d-plane and OTA ports.
    mplane_status set_ether_rx_port(uint16_t port) override;
    rx_test_stats ether_rx_stats(bool clear) override;
    mplane_status ether_tx(const ether_tx_request& req) override;

    mplane_status set_wifi_rx_match(const wifi_rx_match& match) override;
    rx_test_stats wifi_rx_stats(bool clear) override;
    mplane_status wifi_tx(const wifi_tx_request& req) override;

private:
    wifi& radio_;
    ether_test ether_;
    wifi_tx_test wifi_tx_;
};

#endif  // WINJECT_MPLANE_TEST_BACKEND_ESP_H_
