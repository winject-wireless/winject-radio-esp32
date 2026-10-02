#include "settings_blob.h"

#include <string.h>

namespace
{
class blob_writer
{
public:
    explicit blob_writer(uint8_t* buf) : p_(buf) {}

    void u8(uint8_t v)
    {
        *p_++ = v;
    }

    void u16(uint16_t v)
    {
        u8(static_cast<uint8_t>(v));
        u8(static_cast<uint8_t>(v >> 8));
    }

    void raw(const void* data, size_t n)
    {
        memcpy(p_, data, n);
        p_ += n;
    }

private:
    uint8_t* p_;
};

class blob_reader
{
public:
    blob_reader(const uint8_t* buf, size_t len) : p_(buf), end_(buf + len) {}

    bool u8(uint8_t* v)
    {
        if (p_ >= end_)
        {
            return false;
        }
        *v = *p_++;
        return true;
    }

    bool u16(uint16_t* v)
    {
        uint8_t lo = 0;
        uint8_t hi = 0;
        if (!u8(&lo) || !u8(&hi))
        {
            return false;
        }
        *v = static_cast<uint16_t>(lo | (hi << 8));
        return true;
    }

    bool raw(void* out, size_t n)
    {
        if (static_cast<size_t>(end_ - p_) < n)
        {
            return false;
        }
        memcpy(out, p_, n);
        p_ += n;
        return true;
    }

    bool done() const
    {
        return p_ == end_;
    }

private:
    const uint8_t* p_;
    const uint8_t* end_;
};
}  // namespace

size_t settings_blob_pack(const settings_snapshot& snap, uint8_t* buf,
                          size_t cap)
{
    if (buf == nullptr || cap < k_settings_blob_size)
    {
        return 0;
    }
    blob_writer w(buf);
    w.u8(k_settings_blob_version);

    w.u8(static_cast<uint8_t>(snap.network.type));
    // ip stays in network byte order on flash.
    w.raw(&snap.network.ip, sizeof(snap.network.ip));
    w.u8(snap.network.prefix);
    w.u16(snap.network.timeout_s);

    w.u8(snap.radio.channel);
    w.u8(static_cast<uint8_t>(snap.radio.tx_power_dbm));
    w.u8(snap.radio.cca_enabled ? 1 : 0);
    char modulation[SETTINGS_MODULATION_MAX] = {};
    const size_t mod_len =
        strnlen(snap.radio.modulation, sizeof(modulation) - 1);
    memcpy(modulation, snap.radio.modulation, mod_len);
    w.raw(modulation, sizeof(modulation));

    w.u8(snap.rx_filter.enabled ? 1 : 0);
    w.raw(snap.rx_filter.addr, sizeof(snap.rx_filter.addr));

    w.u8(snap.tune.eth_dma_burst_len);
    w.u8(snap.tune.eth_rx_ring_sz);
    w.u8(snap.tune.eth_tx_ring_sz);
    w.u8(snap.tune.tx_queue_sz);
    w.u8(snap.tune.rx_queue_sz);
    w.u8(snap.tune.wifi_tx_ring_sz);
    w.u8(snap.tune.wifi_rx_ring_sz);
    return k_settings_blob_size;
}

bool settings_blob_unpack(const uint8_t* buf, size_t len,
                          settings_snapshot* out)
{
    if (buf == nullptr || out == nullptr || len != k_settings_blob_size)
    {
        return false;
    }
    blob_reader r(buf, len);
    settings_snapshot snap;

    uint8_t version = 0;
    if (!r.u8(&version) || version != k_settings_blob_version)
    {
        return false;
    }

    uint8_t type = 0;
    if (!r.u8(&type) || !r.raw(&snap.network.ip, sizeof(snap.network.ip)) ||
        !r.u8(&snap.network.prefix) || !r.u16(&snap.network.timeout_s))
    {
        return false;
    }
    if (type != static_cast<uint8_t>(network_type::dhcp) &&
        type != static_cast<uint8_t>(network_type::static_ip))
    {
        return false;
    }
    snap.network.type = static_cast<network_type>(type);

    uint8_t tx_power = 0;
    uint8_t cca = 0;
    if (!r.u8(&snap.radio.channel) || !r.u8(&tx_power) || !r.u8(&cca) ||
        !r.raw(snap.radio.modulation, sizeof(snap.radio.modulation)))
    {
        return false;
    }
    if (cca > 1 ||
        memchr(snap.radio.modulation, '\0', sizeof(snap.radio.modulation)) ==
            nullptr)
    {
        return false;
    }
    snap.radio.tx_power_dbm = static_cast<int8_t>(tx_power);
    snap.radio.cca_enabled = cca != 0;

    uint8_t filter_enabled = 0;
    if (!r.u8(&filter_enabled) || filter_enabled > 1 ||
        !r.raw(snap.rx_filter.addr, sizeof(snap.rx_filter.addr)))
    {
        return false;
    }
    snap.rx_filter.enabled = filter_enabled != 0;

    if (!r.u8(&snap.tune.eth_dma_burst_len) ||
        !r.u8(&snap.tune.eth_rx_ring_sz) || !r.u8(&snap.tune.eth_tx_ring_sz) ||
        !r.u8(&snap.tune.tx_queue_sz) || !r.u8(&snap.tune.rx_queue_sz) ||
        !r.u8(&snap.tune.wifi_tx_ring_sz) || !r.u8(&snap.tune.wifi_rx_ring_sz))
    {
        return false;
    }
    if (!r.done())
    {
        return false;
    }
    *out = snap;
    return true;
}

bool settings_blob_unpack_legacy(const uint8_t* buf, size_t len,
                                 settings_snapshot* inout)
{
    if (buf == nullptr || inout == nullptr ||
        len < k_settings_blob_legacy_prefix)
    {
        return false;
    }
    blob_reader r(buf, len);

    uint8_t version = 0;
    if (!r.u8(&version) || version < k_settings_blob_legacy_min ||
        version > k_settings_blob_legacy_max)
    {
        return false;
    }

    uint8_t mode = 0;
    uint8_t channel = 0;
    uint8_t cca = 0;
    uint8_t allow_crc = 0;
    uint8_t tx_power = 0;
    if (!r.u8(&mode) || !r.u8(&channel) || !r.u8(&cca) || !r.u8(&allow_crc) ||
        !r.u8(&tx_power))
    {
        return false;
    }
    (void)mode;
    (void)allow_crc;

    char modulation[SETTINGS_MODULATION_MAX];
    if (!r.raw(modulation, sizeof(modulation)))
    {
        return false;
    }
    modulation[SETTINGS_MODULATION_MAX - 1] = '\0';

    uint32_t ip = 0;
    uint8_t network_mode = 0;
    if (!r.raw(&ip, sizeof(ip)) || !r.u8(&network_mode))
    {
        return false;
    }
    if (network_mode > 1)
    {
        return false;
    }

    settings_snapshot snap = *inout;
    snap.network.type =
        network_mode == 0 ? network_type::static_ip : network_type::dhcp;
    if (ip != 0)
    {
        snap.network.ip = ip;
    }
    snap.network.prefix = 24;
    snap.network.timeout_s = NETWORK_DHCP_TIMEOUT_S_DEFAULT;
    snap.radio.channel = channel;
    snap.radio.tx_power_dbm = static_cast<int8_t>(tx_power);
    snap.radio.cca_enabled = cca != 0;
    strncpy(snap.radio.modulation, modulation, sizeof(snap.radio.modulation) - 1);
    snap.radio.modulation[sizeof(snap.radio.modulation) - 1] = '\0';

    *inout = snap;
    return true;
}
