#ifndef WINJECT_UDP_L2_MATCH_H_
#define WINJECT_UDP_L2_MATCH_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Match IPv4/UDP frames on Ethernet for d-plane inject. No ESP-IDF deps.

// esp_ip4_addr_t::addr / in_addr::s_addr (network byte order in memory)
// -> host-order a<<24|b<<16|c<<8|d, the form udp_l2_match_dst compares.
static inline uint32_t udp_l2_ipv4_host_from_lwip(uint32_t lwip_addr)
{
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&lwip_addr);
    return (static_cast<uint32_t>(b[0]) << 24) |
           (static_cast<uint32_t>(b[1]) << 16) |
           (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
}

static inline bool udp_l2_match_dst(const uint8_t* buffer, uint32_t length,
                                    const uint8_t dst_mac[6],
                                    uint32_t dst_ip_host, uint16_t dst_port,
                                    const uint8_t** payload_out,
                                    uint16_t* payload_len_out)
{
    if (buffer == nullptr || length < 42u || dst_mac == nullptr ||
        payload_out == nullptr || payload_len_out == nullptr)
    {
        return false;
    }
    if (memcmp(buffer, dst_mac, 6) != 0)
    {
        return false;
    }
    if (buffer[12] != 0x08 || buffer[13] != 0x00)
    {
        return false;
    }
    const uint8_t* ip = buffer + 14;
    const uint8_t ver_ihl = ip[0];
    if ((ver_ihl >> 4) != 4)
    {
        return false;
    }
    const uint32_t ihl = static_cast<uint32_t>(ver_ihl & 0x0Fu) * 4u;
    if (ihl < 20u || length < 14u + ihl + 8u)
    {
        return false;
    }
    if (((ip[6] & 0x3Fu) | ip[7]) != 0)
    {
        return false;
    }
    const uint16_t tot_len =
        static_cast<uint16_t>((ip[2] << 8) | ip[3]);
    if (tot_len < ihl + 8u || 14u + tot_len > length)
    {
        return false;
    }
    if (ip[9] != 17)
    {
        return false;
    }
    if (dst_ip_host == 0u)
    {
        return false;
    }
    const uint32_t ip_dst =
        (static_cast<uint32_t>(ip[16]) << 24) |
        (static_cast<uint32_t>(ip[17]) << 16) |
        (static_cast<uint32_t>(ip[18]) << 8) |
        static_cast<uint32_t>(ip[19]);
    if (ip_dst != dst_ip_host)
    {
        return false;
    }
    const uint8_t* udp = ip + ihl;
    const uint16_t dst =
        static_cast<uint16_t>((udp[2] << 8) | udp[3]);
    if (dst != dst_port)
    {
        return false;
    }
    const uint16_t udp_len =
        static_cast<uint16_t>((udp[4] << 8) | udp[5]);
    if (udp_len < 8u || udp_len > tot_len - ihl)
    {
        return false;
    }
    *payload_len_out = static_cast<uint16_t>(udp_len - 8u);
    *payload_out = udp + 8;
    return true;
}

#endif  // WINJECT_UDP_L2_MATCH_H_
