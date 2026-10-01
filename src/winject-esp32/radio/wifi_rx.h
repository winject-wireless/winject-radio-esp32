#ifndef WINJECT_WIFI_RX_H_
#define WINJECT_WIFI_RX_H_

#include "config.h"
#include "config_types.h"
#include "packet.h"

#include <atomic>
#include <optional>
#include <stddef.h>
#include <stdint.h>

#include "bfc-esp32/wait_free_queue.hpp"
#include "esp_wifi_types.h"

class wifi;

// Promiscuous RX. The WiFi callback filters on Addr3, copies MPDU + on-air FCS
// into an RX pool slot, and queues it for the d-plane drain task.
class wifi_rx
{
    friend class wifi;

public:
    static constexpr uint8_t k_queue_max = WIFI_RX_QUEUE_MAX;

    struct test_counters
    {
        uint64_t pkt = 0;
        uint64_t byt = 0;
        uint64_t fcs_error_pkt = 0;
    };

    wifi_rx(const wifi_rx&) = delete;
    wifi_rx& operator=(const wifi_rx&) = delete;

    // capacity: tune rx_queue_sz (1..k_queue_max). Call once before WiFi init.
    bool init(uint8_t capacity);

    uint8_t queue_size() const;
    // Next forwarded frame (MPDU followed by its 4-byte FCS); invalid on
    // timeout.
    packet pop(TickType_t wait);
    void on_upstream_deliver();

    // Enabled: forward only Addr3 == addr. Disabled: forward Addr3 with the
    // WIFI_BSSID_PREFIX_STANDALONE prefix.
    mac_filter filter_addr3() const;
    void set_filter_addr3(const mac_filter& filter);
    // RSSI of the last accepted frame; false before any.
    bool last_rssi(int8_t* dbm) const;

    // Test tap: counts frames whose Addr1/2/3 match (disabled filter = any),
    // independent of the d-plane filter. All disabled = off.
    void set_test_match(const mac_filter& addr1, const mac_filter& addr2,
                        const mac_filter& addr3);
    test_counters test_stats(bool clear);

private:
    explicit wifi_rx(wifi& radio);

    bool apply_monitor();

    static void promiscuous_cb(void* buf, wifi_promiscuous_pkt_type_t type);
    void on_promiscuous(void* buf, wifi_promiscuous_pkt_type_t type);
    bool forward_accept(const uint8_t* mpdu, size_t len) const;
    bool test_accept(const uint8_t* mpdu) const;
    void count_test(const uint8_t* frame, size_t mpdu_len);

    wifi& radio;
    bfc::wait_free_queue<std::optional<packet>, k_queue_max> q;
    std::atomic<uint64_t> filter_addr3_{0};
    std::atomic<bool> test_active_{false};
    std::atomic<uint64_t> test_addr_[3]{};
    std::atomic<uint64_t> test_pkt_{0};
    std::atomic<uint64_t> test_byt_{0};
    std::atomic<uint64_t> test_fcs_err_{0};
    std::atomic<int8_t> rssi_{0};
    std::atomic<bool> rssi_valid_{false};
};

#endif  // WINJECT_WIFI_RX_H_
