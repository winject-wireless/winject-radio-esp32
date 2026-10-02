#include "wifi_rx.h"

#include "config.h"
#include "fcs.h"
#include "packet.h"
#include "wifi.h"

#include <string.h>
#include <utility>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"

static const char* TAG = "wifi_rx";

static constexpr size_t k_addr1_off = 4;
static constexpr size_t k_addr2_off = 10;
static constexpr size_t k_addr3_off = 16;

wifi_rx::wifi_rx(wifi& radio) : radio(radio) {}

bool wifi_rx::init(uint8_t capacity)
{
    if (!q.init(capacity))
    {
        ESP_LOGE(TAG, "rx queue init capacity=%u failed (max %u)",
                 static_cast<unsigned>(capacity),
                 static_cast<unsigned>(k_queue_max));
        return false;
    }
    return true;
}

packet wifi_rx::pop(TickType_t wait)
{
    std::optional<packet> slot;
    if (!q.pop(&slot, wait) || !slot.has_value())
    {
        return packet();
    }
    return std::move(*slot);
}

void wifi_rx::on_upstream_deliver()
{
    radio.pulse_rx_led();
}

uint8_t wifi_rx::queue_size() const
{
    return q.size();
}

uint32_t wifi_rx::dropped_filter_mismatched() const
{
    return dropped_filter_mismatched_.load(std::memory_order_relaxed);
}

uint32_t wifi_rx::dropped_rx_queue() const
{
    return dropped_rx_queue_.load(std::memory_order_relaxed);
}

uint32_t wifi_rx::air_pkt() const
{
    return air_pkt_.load(std::memory_order_relaxed);
}

mac_filter wifi_rx::filter_addr3() const
{
    return mac_filter_unpack(filter_addr3_.load(std::memory_order_relaxed));
}

void wifi_rx::set_filter_addr3(const mac_filter& filter)
{
    filter_addr3_.store(mac_filter_pack(filter), std::memory_order_relaxed);
}

bool wifi_rx::last_rssi(int8_t* dbm) const
{
    if (dbm == nullptr || !rssi_valid_.load(std::memory_order_acquire))
    {
        return false;
    }
    *dbm = rssi_.load(std::memory_order_relaxed);
    return true;
}

void wifi_rx::set_test_match(const mac_filter& addr1, const mac_filter& addr2,
                             const mac_filter& addr3)
{
    // Off while rewriting so the callback never mixes old and new addresses.
    test_active_.store(false, std::memory_order_release);
    test_addr_[0].store(mac_filter_pack(addr1), std::memory_order_relaxed);
    test_addr_[1].store(mac_filter_pack(addr2), std::memory_order_relaxed);
    test_addr_[2].store(mac_filter_pack(addr3), std::memory_order_relaxed);
    test_active_.store(addr1.enabled || addr2.enabled || addr3.enabled,
                       std::memory_order_release);
}

wifi_rx::test_counters wifi_rx::test_stats(bool clear)
{
    test_counters out;
    if (clear)
    {
        out.pkt = test_pkt_.exchange(0, std::memory_order_relaxed);
        out.byt = test_byt_.exchange(0, std::memory_order_relaxed);
        out.fcs_error_pkt = test_fcs_err_.exchange(0, std::memory_order_relaxed);
    }
    else
    {
        out.pkt = test_pkt_.load(std::memory_order_relaxed);
        out.byt = test_byt_.load(std::memory_order_relaxed);
        out.fcs_error_pkt = test_fcs_err_.load(std::memory_order_relaxed);
    }
    return out;
}

bool wifi_rx::forward_accept(const uint8_t* mpdu, size_t len) const
{
    const mac_filter f =
        mac_filter_unpack(filter_addr3_.load(std::memory_order_relaxed));
    if (f.enabled)
    {
        return f.matches(mpdu + k_addr3_off);
    }
    return true;
}

bool wifi_rx::test_accept(const uint8_t* mpdu) const
{
    if (!test_active_.load(std::memory_order_acquire))
    {
        return false;
    }
    static constexpr size_t k_offsets[3] = {k_addr1_off, k_addr2_off,
                                            k_addr3_off};
    for (size_t i = 0; i < 3; ++i)
    {
        const mac_filter f =
            mac_filter_unpack(test_addr_[i].load(std::memory_order_relaxed));
        if (!f.matches(mpdu + k_offsets[i]))
        {
            return false;
        }
    }
    return true;
}

void wifi_rx::count_test(size_t mpdu_len, uint8_t rx_state)
{
    test_pkt_.fetch_add(1, std::memory_order_relaxed);
    test_byt_.fetch_add(mpdu_len, std::memory_order_relaxed);
    if (rx_state != 0)
    {
        test_fcs_err_.fetch_add(1, std::memory_order_relaxed);
    }
}

void wifi_rx::on_promiscuous(void* buf, wifi_promiscuous_pkt_type_t type)
{
    if (buf == nullptr)
    {
        return;
    }
    air_pkt_.fetch_add(1, std::memory_order_relaxed);
    // HT MCS inject may be classified as CTRL/MISC on ESP32 promisc; Addr3 match
    // still uses the MPDU in payload when sig_len is valid.
    if (type != WIFI_PKT_DATA && type != WIFI_PKT_MISC && type != WIFI_PKT_CTRL)
    {
        dropped_filter_mismatched_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    const wifi_pkt_rx_ctrl_t& rx = pkt->rx_ctrl;
    // Drop only true multi-subframe A-MPDUs. HT MCS often sets aggregation=1
    // for a single MPDU; ampdu_cnt alone is not a reliable drop signal.
    if (rx.aggregation != 0 && rx.ampdu_cnt > 1)
    {
        dropped_filter_mismatched_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // sig_len includes the on-air FCS, but ESP-IDF does not copy it into
    // payload; we append a fcs=SIGNAL trailer instead (see write_fcs_signal).
    const size_t total = rx.sig_len;
    if (total < WIFI_HDR_LEN + WIFI_FCS_LEN || total > WIFI_RX_PACKET_CAP)
    {
        dropped_filter_mismatched_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const size_t mpdu_len = total - WIFI_FCS_LEN;
    const uint8_t* frame = pkt->payload;

    const bool test_hit = test_accept(frame);
    if (test_hit)
    {
        count_test(mpdu_len, static_cast<uint8_t>(rx.rx_state));
    }
    const bool forward = forward_accept(frame, mpdu_len);
    if (!forward && !test_hit)
    {
        dropped_filter_mismatched_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    rssi_.store(static_cast<int8_t>(rx.rssi), std::memory_order_relaxed);
    rssi_valid_.store(true, std::memory_order_release);
    if (!forward)
    {
        dropped_filter_mismatched_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    packet p = packet_allocator::rx().allocate();
    if (!p.is_valid())
    {
        dropped_rx_queue_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    memcpy(p.data(), frame, total);
    write_fcs_signal(p.data() + mpdu_len, static_cast<uint8_t>(rx.rx_state));
    p.set_packet_size(total);
    std::optional<packet> item(std::move(p));
    if (!q.try_push(std::move(item)))
    {
        dropped_rx_queue_.fetch_add(1, std::memory_order_relaxed);
        if (item.has_value())
        {
            item->reset();
        }
    }
}

void wifi_rx::promiscuous_cb(void* buf, wifi_promiscuous_pkt_type_t type)
{
    wifi::instance().rx().on_promiscuous(buf, type);
}

bool wifi_rx::apply_monitor()
{
    wifi_promiscuous_filter_t filter = {};
    // Raw-inject peers are delivered as DATA/MISC with spurious rx_state==0x41.
    // FCSFAIL must stay enabled on ESP32 or Addr3-matched inject frames never
    // reach the CB; the forwarded FCS lets the peer judge integrity.
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA |
                         WIFI_PROMIS_FILTER_MASK_DATA_MPDU |
                         WIFI_PROMIS_FILTER_MASK_DATA_AMPDU |
                         WIFI_PROMIS_FILTER_MASK_MISC |
                         WIFI_PROMIS_FILTER_MASK_FCSFAIL;
    esp_err_t err = esp_wifi_set_promiscuous_filter(&filter);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "promiscuous filter failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_wifi_set_promiscuous_rx_cb(promiscuous_cb);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "promiscuous cb failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_wifi_set_promiscuous(true);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "promiscuous failed: %s", esp_err_to_name(err));
        return false;
    }
    (void)radio.apply_sta_decode_protocol();
    return true;
}
