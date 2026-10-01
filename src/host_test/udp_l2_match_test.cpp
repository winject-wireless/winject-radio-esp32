#include "config.h"
#include "udp_l2_match.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace
{

std::vector<uint8_t> make_udp_frame(const uint8_t dst_mac[6], uint32_t dst_ip_be,
                                    uint32_t src_ip_be, uint16_t dst_port,
                                    uint16_t payload_len)
{
    const uint8_t src_mac[6] = {0x02, 0, 0, 0, 0, 1};
    std::vector<uint8_t> frame(14 + 20 + 8 + payload_len);
    memcpy(frame.data(), dst_mac, 6);
    memcpy(frame.data() + 6, src_mac, 6);
    frame[12] = 0x08;
    frame[13] = 0x00;
    uint8_t* ip = frame.data() + 14;
    ip[0] = 0x45;
    const uint16_t tot = static_cast<uint16_t>(20 + 8 + payload_len);
    ip[2] = static_cast<uint8_t>(tot >> 8);
    ip[3] = static_cast<uint8_t>(tot & 0xff);
    ip[9] = 17;
    ip[12] = static_cast<uint8_t>((src_ip_be >> 24) & 0xff);
    ip[13] = static_cast<uint8_t>((src_ip_be >> 16) & 0xff);
    ip[14] = static_cast<uint8_t>((src_ip_be >> 8) & 0xff);
    ip[15] = static_cast<uint8_t>(src_ip_be & 0xff);
    ip[16] = static_cast<uint8_t>((dst_ip_be >> 24) & 0xff);
    ip[17] = static_cast<uint8_t>((dst_ip_be >> 16) & 0xff);
    ip[18] = static_cast<uint8_t>((dst_ip_be >> 8) & 0xff);
    ip[19] = static_cast<uint8_t>(dst_ip_be & 0xff);
    uint8_t* udp = ip + 20;
    udp[2] = static_cast<uint8_t>(dst_port >> 8);
    udp[3] = static_cast<uint8_t>(dst_port & 0xff);
    const uint16_t udp_len = static_cast<uint16_t>(8 + payload_len);
    udp[4] = static_cast<uint8_t>(udp_len >> 8);
    udp[5] = static_cast<uint8_t>(udp_len & 0xff);
    return frame;
}

uint32_t ip_be(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d);
}

}  // namespace

TEST(UdpL2Match, ValidFrame)
{
    const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const uint32_t dst = ip_be(192, 168, 1, 10);
    const uint32_t src = ip_be(192, 168, 1, 1);
    auto frame = make_udp_frame(mac, dst, src, DPLANE_INJECT_PORT, 100);
    const uint8_t* payload = nullptr;
    uint16_t len = 0;
    EXPECT_TRUE(udp_l2_match_dst(frame.data(), static_cast<uint32_t>(frame.size()),
                                 mac, dst, 0u, DPLANE_INJECT_PORT, &payload,
                                 &len));
    EXPECT_EQ(len, 100u);
    EXPECT_EQ(payload, frame.data() + 14 + 20 + 8);
}

TEST(UdpL2Match, WrongDestMac)
{
    const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    const uint8_t other[6] = {9, 9, 9, 9, 9, 9};
    const uint32_t dst = ip_be(10, 0, 0, 1);
    auto frame = make_udp_frame(other, dst, ip_be(10, 0, 0, 2),
                                DPLANE_INJECT_PORT, 24);
    const uint8_t* payload = nullptr;
    uint16_t len = 0;
    EXPECT_FALSE(udp_l2_match_dst(frame.data(), static_cast<uint32_t>(frame.size()),
                                  mac, dst, 0u, DPLANE_INJECT_PORT, &payload,
                                  &len));
}

TEST(UdpL2Match, UntrustedSource)
{
    const uint8_t mac[6] = {1, 1, 1, 1, 1, 1};
    const uint32_t dst = ip_be(192, 168, 32, 1);
    const uint32_t src = ip_be(192, 168, 32, 99);
    auto frame = make_udp_frame(mac, dst, src, DPLANE_INJECT_PORT, 32);
    const uint8_t* payload = nullptr;
    uint16_t len = 0;
    const uint32_t trusted = 0xC0A82001u;  // 192.168.32.1 host order
    EXPECT_FALSE(udp_l2_match_dst(frame.data(), static_cast<uint32_t>(frame.size()),
                                    mac, dst, trusted, DPLANE_INJECT_PORT,
                                    &payload, &len));
    EXPECT_TRUE(udp_l2_match_dst(frame.data(), static_cast<uint32_t>(frame.size()),
                                 mac, dst, 0u, DPLANE_INJECT_PORT, &payload,
                                 &len));
}

TEST(UdpL2Match, FragmentRejected)
{
    const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    const uint32_t dst = ip_be(10, 0, 0, 1);
    auto frame = make_udp_frame(mac, dst, ip_be(10, 0, 0, 2),
                                DPLANE_INJECT_PORT, 24);
    frame[14 + 6] |= 0x20;  // MF
    const uint8_t* payload = nullptr;
    uint16_t len = 0;
    EXPECT_FALSE(udp_l2_match_dst(frame.data(), static_cast<uint32_t>(frame.size()),
                                  mac, dst, 0u, DPLANE_INJECT_PORT, &payload,
                                  &len));
}

TEST(UdpL2Match, ShortFrame)
{
    const uint8_t mac[6] = {};
    const uint8_t* payload = nullptr;
    uint16_t len = 0;
    uint8_t buf[20] = {};
    EXPECT_FALSE(udp_l2_match_dst(buf, sizeof(buf), mac, 1u, 0u,
                                  DPLANE_INJECT_PORT, &payload, &len));
}
