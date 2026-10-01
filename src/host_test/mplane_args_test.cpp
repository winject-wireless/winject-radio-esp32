#include "mplane_args.h"

#include <gtest/gtest.h>

#include <string>

namespace
{
uint32_t ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    uint32_t out = 0;
    auto* p = reinterpret_cast<uint8_t*>(&out);
    p[0] = a;
    p[1] = b;
    p[2] = c;
    p[3] = d;
    return out;
}
}  // namespace

TEST(MplaneArgsTest, ParsesKeyValuePairs)
{
    std::string text = "  channel=6\ttx_power=-2 addr= ";
    mplane_args args;
    ASSERT_TRUE(args.parse(text.data()));
    EXPECT_EQ(args.size(), 3u);
    EXPECT_STREQ(args.find("CHANNEL"), "6");
    EXPECT_STREQ(args.find("tx_power"), "-2");
    EXPECT_STREQ(args.find("addr"), "");
    EXPECT_EQ(args.find("missing"), nullptr);
}

TEST(MplaneArgsTest, EmptyTextHasNoArgs)
{
    std::string text = "   ";
    mplane_args args;
    ASSERT_TRUE(args.parse(text.data()));
    EXPECT_TRUE(args.empty());
}

TEST(MplaneArgsTest, RejectsMalformedTokens)
{
    mplane_args args;
    std::string positional = "channel";
    EXPECT_FALSE(args.parse(positional.data()));
    std::string no_key = "=5";
    EXPECT_FALSE(args.parse(no_key.data()));
    std::string dup = "a=1 A=2";
    EXPECT_FALSE(args.parse(dup.data()));
    std::string many = "a=1 b=2 c=3 d=4 e=5 f=6 g=7 h=8 i=9";
    EXPECT_FALSE(args.parse(many.data()));
}

TEST(MplaneArgsTest, OnlyChecksAllowedKeys)
{
    std::string text = "port=1 extra=2";
    mplane_args args;
    ASSERT_TRUE(args.parse(text.data()));
    const char* const port_only[] = {"port", nullptr};
    const char* const both[] = {"port", "extra", nullptr};
    EXPECT_FALSE(args.only(port_only));
    EXPECT_TRUE(args.only(both));
}

TEST(MplaneArgsTest, TypedGettersReportAbsentOkInvalid)
{
    std::string text = "n=300 b=yes bad=abc neg=-5";
    mplane_args args;
    ASSERT_TRUE(args.parse(text.data()));
    unsigned long u = 0;
    long i = 0;
    bool b = false;
    EXPECT_EQ(args.get_uint("missing", 0, 10, &u), mplane_args::status::absent);
    EXPECT_EQ(args.get_uint("n", 0, 255, &u), mplane_args::status::invalid);
    EXPECT_EQ(args.get_uint("n", 0, 300, &u), mplane_args::status::ok);
    EXPECT_EQ(u, 300u);
    EXPECT_EQ(args.get_uint("neg", 0, 300, &u), mplane_args::status::invalid);
    EXPECT_EQ(args.get_int("neg", -10, 10, &i), mplane_args::status::ok);
    EXPECT_EQ(i, -5);
    EXPECT_EQ(args.get_bool("b", &b), mplane_args::status::ok);
    EXPECT_TRUE(b);
    EXPECT_EQ(args.get_bool("bad", &b), mplane_args::status::invalid);
}

TEST(MplaneParseTest, Ipv4)
{
    uint32_t out = 0;
    EXPECT_TRUE(mplane_parse_ipv4("192.168.32.1", &out));
    EXPECT_EQ(out, ip(192, 168, 32, 1));
    EXPECT_FALSE(mplane_parse_ipv4("192.168.32", &out));
    EXPECT_FALSE(mplane_parse_ipv4("192.168.32.256", &out));
    EXPECT_FALSE(mplane_parse_ipv4("192.168.32.1x", &out));
    EXPECT_FALSE(mplane_parse_ipv4("1234.1.1.1", &out));
    EXPECT_FALSE(mplane_parse_ipv4("", &out));
}

TEST(MplaneParseTest, Ipv4Prefix)
{
    uint32_t addr = 0;
    uint8_t prefix = 99;
    bool has_prefix = true;
    EXPECT_TRUE(
        mplane_parse_ipv4_prefix("10.0.0.2/8", &addr, &prefix, &has_prefix));
    EXPECT_EQ(addr, ip(10, 0, 0, 2));
    EXPECT_EQ(prefix, 8);
    EXPECT_TRUE(has_prefix);

    prefix = 99;
    EXPECT_TRUE(
        mplane_parse_ipv4_prefix("10.0.0.3", &addr, &prefix, &has_prefix));
    EXPECT_FALSE(has_prefix);
    EXPECT_EQ(prefix, 99);

    EXPECT_FALSE(
        mplane_parse_ipv4_prefix("10.0.0.3/33", &addr, &prefix, &has_prefix));
    EXPECT_FALSE(
        mplane_parse_ipv4_prefix("10.0.0.3/", &addr, &prefix, &has_prefix));
}

TEST(MplaneParseTest, MacFormats)
{
    uint8_t mac[6] = {};
    const uint8_t want[6] = {0xca, 0xfe, 0xba, 0xbe, 0x00, 0x1f};
    EXPECT_TRUE(mplane_parse_mac("ca:fe:ba:be:00:1f", mac));
    EXPECT_EQ(memcmp(mac, want, 6), 0);
    EXPECT_TRUE(mplane_parse_mac("CA-FE-BA-BE-00-1F", mac));
    EXPECT_EQ(memcmp(mac, want, 6), 0);
    EXPECT_TRUE(mplane_parse_mac("cafebabe001f", mac));
    EXPECT_EQ(memcmp(mac, want, 6), 0);
    EXPECT_FALSE(mplane_parse_mac("ca:fe:ba:be:00", mac));
    EXPECT_FALSE(mplane_parse_mac("ca:fe-ba:be:00:1f", mac));
    EXPECT_FALSE(mplane_parse_mac("zz:fe:ba:be:00:1f", mac));

    char text[18];
    mplane_format_mac(want, text, sizeof(text));
    EXPECT_STREQ(text, "ca:fe:ba:be:00:1f");
}
