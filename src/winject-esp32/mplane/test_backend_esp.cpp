#include "test_backend_esp.h"

#include "config.h"
#include "wifi.h"

test_backend_esp::test_backend_esp(wifi& radio) : radio_(radio), wifi_tx_(radio)
{
}

mplane_status test_backend_esp::set_ether_rx_port(uint16_t port)
{
    if (port == CONTROL_CONSOLE_PORT || port == OTA_HTTP_PORT ||
        port == DPLANE_PORT)
    {
        return mplane_status::invalid;
    }
    return ether_.set_rx_port(port);
}

rx_test_stats test_backend_esp::ether_rx_stats(bool clear)
{
    return ether_.rx_stats(clear);
}

mplane_status test_backend_esp::ether_tx(const ether_tx_request& req)
{
    return ether_.tx(req);
}

mplane_status test_backend_esp::set_wifi_rx_match(const wifi_rx_match& match)
{
    if (!radio_.ready())
    {
        return mplane_status::no_device;
    }
    radio_.rx().set_test_match(match.addr1, match.addr2, match.addr3);
    return mplane_status::ok;
}

rx_test_stats test_backend_esp::wifi_rx_stats(bool clear)
{
    const wifi_rx::test_counters c = radio_.rx().test_stats(clear);
    rx_test_stats out;
    out.pkt = c.pkt;
    out.byt = c.byt;
    out.fec_error_pkt = c.fcs_error_pkt;
    return out;
}

mplane_status test_backend_esp::wifi_tx(const wifi_tx_request& req)
{
    return wifi_tx_.run(req);
}
