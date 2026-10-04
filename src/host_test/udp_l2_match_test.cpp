#include "config.h"
#include "udp_l2_match.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace
{

std::vector<uint8_t> make_udp_frame(const uint8_t dst_mac[6], uint32_t dst_ip_host,
                                    uint32_t src_ip_host, uint16_t dst_port,
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
    ip[12] = static_cast<uint8_t>((src_ip_host >> 24) & 0xff);
    ip[13] = static_cast<uint8_t>((src_ip_host >> 16) & 0xff);
    ip[14] = static_cast<uint8_t>((src_ip_host >> 8) & 0xff);
    ip[15] = static_cast<uint8_t>(src_ip_host & 0xff);
    ip[16] = static_cast<uint8_t>((dst_ip_host >> 24) & 0xff);
    ip[17] = static_cast<uint8_t>((dst_ip_host >> 16) & 0xff);
    ip[18] = static_cast<uint8_t>((dst_ip_host >> 8) & 0xff);
    ip[19] = static_cast<uint8_t>(dst_ip_host & 0xff);
    uint8_t* udp = ip + 20;
    udp[2] = static_cast<uint8_t>(dst_port >> 8);
    udp[3] = static_cast<uint8_t>(dst_port & 0xff);
    const uint16_t udp_len = static_cast<uint16_t>(8 + payload_len);
    udp[4] = static_cast<uint8_t>(udp_len >> 8);
    udp[5] = static_cast<uint8_t>(udp_len & 0xff);
    return frame;
}

uint32_t ip_host(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d);
}

}  // namespace

TEST(UdpL2Match, ValidFrame)
{
    const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const uint32_t dst = ip_host(192, 168, 1, 10);
    const uint32_t src = ip_host(192, 168, 1, 1);
    auto frame = make_udp_frame(mac, dst, src, DPLANE_PORT, 100);
    const uint8_t* payload = nullptr;
    uint16_t len = 0;
    EXPECT_TRUE(udp_l2_match_dst(frame.data(), static_cast<uint32_t>(frame.size()),
                                 mac, dst, DPLANE_PORT, &payload,
                                 &len));
    EXPECT_EQ(len, 100u);
    EXPECT_EQ(payload, frame.data() + 14 + 20 + 8);
}

TEST(UdpL2Match, WrongDestMac)
{
    const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    const uint8_t other[6] = {9, 9, 9, 9, 9, 9};
    const uint32_t dst = ip_host(10, 0, 0, 1);
    auto frame = make_udp_frame(other, dst, ip_host(10, 0, 0, 2),
                                DPLANE_PORT, 24);
    const uint8_t* payload = nullptr;
    uint16_t len = 0;
    EXPECT_FALSE(udp_l2_match_dst(frame.data(), static_cast<uint32_t>(frame.size()),
                                  mac, dst, DPLANE_PORT, &payload,
                                  &len));
}

TEST(UdpL2Match, WrongDestIp)
{
    const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    const uint32_t frame_dst = ip_host(10, 0, 0, 1);
    const uint32_t radio_ip = ip_host(10, 0, 0, 99);
    auto frame = make_udp_frame(mac, frame_dst, ip_host(10, 0, 0, 2),
                                DPLANE_PORT, 24);
    const uint8_t* payload = nullptr;
    uint16_t len = 0;
    EXPECT_FALSE(udp_l2_match_dst(frame.data(), static_cast<uint32_t>(frame.size()),
                                  mac, radio_ip, DPLANE_PORT, &payload,
                                  &len));
}

TEST(UdpL2Match, FragmentRejected)
{
    const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    const uint32_t dst = ip_host(10, 0, 0, 1);
    auto frame = make_udp_frame(mac, dst, ip_host(10, 0, 0, 2),
                                DPLANE_PORT, 24);
    frame[14 + 6] |= 0x20;  // MF
    const uint8_t* payload = nullptr;
    uint16_t len = 0;
    EXPECT_FALSE(udp_l2_match_dst(frame.data(), static_cast<uint32_t>(frame.size()),
                                  mac, dst, DPLANE_PORT, &payload,
                                  &len));
}

TEST(UdpL2Match, ShortFrame)
{
    const uint8_t mac[6] = {};
    const uint8_t* payload = nullptr;
    uint16_t len = 0;
    uint8_t buf[20] = {};
    EXPECT_FALSE(udp_l2_match_dst(buf, sizeof(buf), mac, 1u,
                                  DPLANE_PORT, &payload, &len));
}

TEST(UdpL2Match, LwipAddrMatches)
{
    const uint8_t bytes[4] = {192, 168, 253, 11};
    uint32_t lwip = 0;
    memcpy(&lwip, bytes, sizeof(lwip));
    EXPECT_EQ(udp_l2_ipv4_host_from_lwip(lwip), 0xC0A8FD0Bu);

    const uint8_t mac[6] = {0x20, 0x50, 0x0d, 0x30, 0x39, 0x23};
    const uint32_t dst_host = udp_l2_ipv4_host_from_lwip(lwip);
    auto frame = make_udp_frame(mac, dst_host, ip_host(192, 168, 253, 1),
                                DPLANE_PORT, 200);
    const uint8_t* payload = nullptr;
    uint16_t len = 0;
    EXPECT_TRUE(udp_l2_match_dst(frame.data(), static_cast<uint32_t>(frame.size()),
                                 mac, dst_host, DPLANE_PORT, &payload,
                                 &len));
    EXPECT_EQ(len, 200u);
}

TEST(UdpL2Match, LwipAddrZero)
{
    EXPECT_EQ(udp_l2_ipv4_host_from_lwip(0), 0u);
    const uint8_t mac[6] = {};
    const uint8_t* payload = nullptr;
    uint16_t len = 0;
    auto frame = make_udp_frame(mac, ip_host(1, 2, 3, 4), ip_host(5, 6, 7, 8),
                                DPLANE_PORT, 24);
    EXPECT_FALSE(udp_l2_match_dst(frame.data(), static_cast<uint32_t>(frame.size()),
                                  mac, 0u, DPLANE_PORT, &payload,
                                  &len));
}
