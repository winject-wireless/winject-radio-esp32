#include "mplane_commands.h"

#include "mplane_reply.h"

#include <gtest/gtest.h>

#include <string>
#include <string.h>

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

class string_reply : public mplane_reply
{
public:
    using mplane_reply::write;

    void write(const char* data, size_t n) override
    {
        text.append(data, n);
    }

    std::string text;
};

class fake_device : public mplane_device_backend
{
public:
    fake_device()
    {
        network_.ip = ip(192, 168, 32, 1);
        tune_.eth_rx_ring_sz = 28;
        tune_.eth_tx_ring_sz = 16;
        tune_.wifi_tx_ring_sz = 16;
        tune_.wifi_rx_ring_sz = 32;
    }

    mplane_status restart(std::optional<WinjectMode> mode) override
    {
        ++restarts;
        restart_mode = mode;
        return mplane_status::ok;
    }

    mplane_status accept_reset_id(uint8_t id) override
    {
        if (last_reset_id_valid_ && last_reset_id_ == id)
        {
            return mplane_status::already;
        }
        last_reset_id_valid_ = true;
        last_reset_id_ = id;
        return mplane_status::ok;
    }

    network_config network() const override
    {
        return network_;
    }

    mplane_status set_network(const network_config& cfg) override
    {
        network_ = cfg;
        return mplane_status::ok;
    }

    tune_config tune() const override
    {
        return tune_;
    }

    mplane_status set_tune(const tune_config& cfg) override
    {
        tune_limits l;
        l.eth_rx_ring_sz = 28;
        l.eth_tx_ring_sz = 16;
        l.wifi_rx_ring_min = 8;
        if (!tune_config_valid(cfg, l))
        {
            return mplane_status::invalid;
        }
        tune_ = cfg;
        return mplane_status::ok;
    }

    mplane_status save(uint8_t slot) override
    {
        saved_slot = slot;
        return mplane_status::ok;
    }

    mplane_status load(uint8_t slot) override
    {
        return slot == 3 ? mplane_status::ok : mplane_status::not_found;
    }

    int64_t uptime_us() const override
    {
        return uptime_us_;
    }

    int restarts = 0;
    int64_t uptime_us_ = 123456789;
    std::optional<WinjectMode> restart_mode;
    int saved_slot = -1;
    bool last_reset_id_valid_ = false;
    uint8_t last_reset_id_ = 0;

private:
    network_config network_;
    tune_config tune_;
};

class fake_radio : public mplane_radio_backend
{
public:
    uint8_t tx_queue_size() const override
    {
        return 3;
    }
    uint8_t tx_in_flight() const override
    {
        return 2;
    }
    uint8_t rx_queue_size() const override
    {
        return 1;
    }

    uint32_t tx_dropped_invalid_frame() const override
    {
        return tx_dropped_invalid_frame_;
    }
    uint32_t tx_dropped_tx_queue() const override
    {
        return tx_dropped_tx_queue_;
    }
    uint32_t tx_dropped_wifi() const override
    {
        return tx_dropped_wifi_;
    }
    uint32_t rx_dropped_filter_mismatched() const override
    {
        return rx_dropped_filter_mismatched_;
    }
    uint32_t rx_dropped_rx_queue() const override
    {
        return rx_dropped_rx_queue_;
    }
    uint32_t rx_dropped_no_peer() const override
    {
        return rx_dropped_no_peer_;
    }
    uint32_t rx_dropped_send_failed() const override
    {
        return rx_dropped_send_failed_;
    }
    uint32_t tx_ether_pkt() const override
    {
        return tx_ether_pkt_;
    }
    uint32_t rx_ether_pkt() const override
    {
        return rx_ether_pkt_;
    }
    uint32_t tx_air_pkt() const override
    {
        return tx_air_pkt_;
    }
    uint32_t rx_air_pkt() const override
    {
        return rx_air_pkt_;
    }

    radio_config radio() const override
    {
        return radio_;
    }

    bool rx_rssi(int8_t* dbm) const override
    {
        if (!have_rssi)
        {
            return false;
        }
        *dbm = -42;
        return true;
    }

    radio_caps caps() const override
    {
        return radio_caps{fcs_mode_};
    }

    mplane_status set_radio(const radio_patch& patch) override
    {
        ++set_radio_calls;
        if (patch.modulation != nullptr)
        {
            if (strcmp(patch.modulation, "BOGUS") == 0)
            {
                return mplane_status::invalid;
            }
            strncpy(radio_.modulation, patch.modulation,
                    sizeof(radio_.modulation) - 1);
        }
        if (patch.channel)
        {
            radio_.channel = *patch.channel;
        }
        if (patch.tx_power_dbm)
        {
            radio_.tx_power_dbm = *patch.tx_power_dbm;
        }
        if (patch.cca_enabled)
        {
            radio_.cca_enabled = *patch.cca_enabled;
        }
        return mplane_status::ok;
    }

    mac_filter rx_filter_addr3() const override
    {
        return filter_;
    }

    mplane_status set_rx_filter_addr3(const mac_filter& filter) override
    {
        filter_ = filter;
        return mplane_status::ok;
    }

    const char* modulation_list() const override
    {
        return "DSS_1M_L OFDM_6M";
    }

    bool have_rssi = false;
    int set_radio_calls = 0;
    fcs_mode fcs_mode_ = fcs_mode::signal;
    uint32_t tx_dropped_invalid_frame_ = 3;
    uint32_t tx_dropped_tx_queue_ = 4;
    uint32_t tx_dropped_wifi_ = 5;
    uint32_t rx_dropped_filter_mismatched_ = 14;
    uint32_t rx_dropped_rx_queue_ = 7;
    uint32_t rx_dropped_no_peer_ = 8;
    uint32_t rx_dropped_send_failed_ = 9;
    uint32_t tx_ether_pkt_ = 12;
    uint32_t rx_ether_pkt_ = 13;
    uint32_t tx_air_pkt_ = 10;
    uint32_t rx_air_pkt_ = 11;

private:
    radio_config radio_;
    mac_filter filter_;
};

class fake_test : public mplane_test_backend
{
public:
    mplane_status set_ether_rx_port(uint16_t port) override
    {
        ether_rx_port = port;
        return mplane_status::ok;
    }

    rx_test_stats ether_rx_stats(bool clear) override
    {
        last_clear = clear;
        rx_test_stats s;
        s.pkt = 10;
        s.byt = 14000;
        return s;
    }

    mplane_status ether_tx(const ether_tx_request& req) override
    {
        ether_req = req;
        return next_status;
    }

    mplane_status set_wifi_rx_match(const wifi_rx_match& match) override
    {
        wifi_match = match;
        return mplane_status::ok;
    }

    rx_test_stats wifi_rx_stats(bool clear) override
    {
        last_clear = clear;
        rx_test_stats s;
        s.pkt = 5;
        s.byt = 500;
        s.fec_error_pkt = 1;
        return s;
    }

    mplane_status wifi_tx(const wifi_tx_request& req) override
    {
        wifi_req = req;
        return next_status;
    }

    uint16_t ether_rx_port = 1;
    bool last_clear = false;
    ether_tx_request ether_req;
    wifi_rx_match wifi_match;
    wifi_tx_request wifi_req;
    mplane_status next_status = mplane_status::ok;
};

class MplaneCommandsTest : public ::testing::Test
{
protected:
    std::string run(const char* line)
    {
        std::string buf(line);
        string_reply reply;
        commands.handle_text(buf.data(), reply);
        return reply.text;
    }

    fake_device device;
    fake_radio radio;
    fake_test test;
    mplane_commands commands{device, &radio, &test};
};
}  // namespace

TEST_F(MplaneCommandsTest, PingAndAlias)
{
    EXPECT_EQ(run("ping"), "pong\n");
    EXPECT_EQ(run("P\r\n"), "pong\n");
    EXPECT_EQ(run("ping extra"), "NOK EINVAL\n");
}

TEST_F(MplaneCommandsTest, UnknownCommandAndComments)
{
    EXPECT_EQ(run("status"), "NOK ENOSYS\n");
    EXPECT_EQ(run("# comment"), "");
    EXPECT_EQ(run(""), "");
}

TEST_F(MplaneCommandsTest, MultipleLinesInOneDatagram)
{
    EXPECT_EQ(
        run("ping\nti\n"),
        "pong\ntx_info tx_queue_sz=3 in_flight=2 dropped_invalid_frame=3 "
        "dropped_tx_queue=4 dropped_wifi=5 ether_pkt=12 air_pkt=10 "
        "ts=123456789\n");
}

TEST_F(MplaneCommandsTest, HelpListsCommandsAndModulations)
{
    const std::string out = run("?");
    EXPECT_NE(out.find("tune_rx_param|trp "), std::string::npos);
    EXPECT_NE(out.find("save <slot"), std::string::npos);
    EXPECT_NE(out.find("modulations: DSS_1M_L OFDM_6M\n"), std::string::npos);
}

TEST_F(MplaneCommandsTest, ResetWithAndWithoutMode)
{
    EXPECT_EQ(run("reset"), "OK\n");
    EXPECT_FALSE(device.restart_mode.has_value());
    EXPECT_EQ(run("r mode=ota"), "OK\n");
    ASSERT_TRUE(device.restart_mode.has_value());
    EXPECT_EQ(*device.restart_mode, WINJECT_MODE_OTA);
    EXPECT_EQ(run("r mode=WINJECT"), "OK\n");
    EXPECT_EQ(*device.restart_mode, WINJECT_MODE_STANDALONE);
    EXPECT_EQ(run("reset mode=STANDALONE"), "NOK EINVAL\n");
    EXPECT_EQ(run("reset id=3"), "OK id=3\n");
    EXPECT_EQ(run("reset id=3"), "NOK EALREADY\n");
    EXPECT_EQ(run("reset id=4"), "OK id=4\n");
    EXPECT_EQ(device.restarts, 5);
}

TEST_F(MplaneCommandsTest, SaveAndLoadSlots)
{
    EXPECT_EQ(run("save 4"), "OK\n");
    EXPECT_EQ(device.saved_slot, 4);
    EXPECT_EQ(run("save"), "NOK EINVAL\n");
    EXPECT_EQ(run("save 10"), "NOK EINVAL\n");
    EXPECT_EQ(run("save 1 2"), "NOK EINVAL\n");
    EXPECT_EQ(run("load 3"), "OK\n");
    EXPECT_EQ(run("load 2"), "NOK ENOENT\n");
}

TEST_F(MplaneCommandsTest, SetNetworkQueryAndUpdate)
{
    EXPECT_EQ(run("sn"),
              "OK network ip=192.168.32.1/24 type=dhcp timeout=5\n");
    EXPECT_EQ(run("network ip=10.1.0.9/16 type=static timeout=0"),
              "OK network ip=10.1.0.9/16 type=static timeout=0\n");
    EXPECT_EQ(device.network().ip, ip(10, 1, 0, 9));
    EXPECT_EQ(device.network().type, network_type::static_ip);
    // Prefix kept when omitted.
    EXPECT_EQ(run("sn ip=10.1.0.10"),
              "OK network ip=10.1.0.10/16 type=static timeout=0\n");
}

TEST_F(MplaneCommandsTest, SetNetworkRejectsBadValues)
{
    EXPECT_EQ(run("sn type=auto"), "NOK EINVAL\n");
    EXPECT_EQ(run("sn ip=10.1.0.0/16"), "NOK EINVAL\n");
    EXPECT_EQ(run("sn ip=10.1.0.300/16"), "NOK EINVAL\n");
    EXPECT_EQ(run("sn timeout=70000"), "NOK EINVAL\n");
    EXPECT_EQ(run("sn mode=static"), "NOK EINVAL\n");
    EXPECT_EQ(device.network().ip, ip(192, 168, 32, 1));
}

TEST_F(MplaneCommandsTest, TuneCommandsUpdateAndEcho)
{
    EXPECT_EQ(run("tp set_eth_dma_burst_len=8"),
              "OK tune_param set_eth_dma_burst_len=8\n");
    EXPECT_EQ(run("ttp tx_queue_sz=40 wifi_tx_ring_sz=24"),
              "OK tune_tx_param eth_rx_ring_sz=28 tx_queue_sz=40 "
              "wifi_tx_ring_sz=24\n");
    EXPECT_EQ(run("tune_rx_param rx_queue_sz=12"),
              "OK tune_rx_param eth_tx_ring_sz=16 rx_queue_sz=12 "
              "wifi_rx_ring_sz=32\n");
    EXPECT_EQ(run("trp"),
              "OK tune_rx_param eth_tx_ring_sz=16 rx_queue_sz=12 "
              "wifi_rx_ring_sz=32\n");
}

TEST_F(MplaneCommandsTest, TuneCommandsRejectInvalid)
{
    EXPECT_EQ(run("tp set_eth_dma_burst_len=3"), "NOK EINVAL\n");
    EXPECT_EQ(run("ttp eth_rx_ring_sz=10"), "NOK EINVAL\n");
    EXPECT_EQ(run("ttp tx_queue_sz=256"), "NOK EINVAL\n");
    EXPECT_EQ(run("ttp rx_queue_sz=4"), "NOK EINVAL\n");
    EXPECT_EQ(run("trp wifi_rx_ring_sz=2"), "NOK EINVAL\n");
    EXPECT_EQ(device.tune().eth_dma_burst_len, 32);
}

TEST_F(MplaneCommandsTest, QueueInfo)
{
    EXPECT_EQ(
        run("tx_info"),
        "tx_info tx_queue_sz=3 in_flight=2 dropped_invalid_frame=3 "
        "dropped_tx_queue=4 dropped_wifi=5 ether_pkt=12 air_pkt=10 "
        "ts=123456789\n");
    EXPECT_EQ(
        run("ri"),
        "rx_info rx_queue_sz=1 dropped_filter_mismatched=14 "
        "dropped_rx_queue=7 dropped_no_peer=8 dropped_send_failed=9 "
        "ether_pkt=13 air_pkt=11 ts=123456789\n");
    EXPECT_EQ(run("ri now=1"), "NOK EINVAL\n");
}

TEST_F(MplaneCommandsTest, QueueInfoLargeCountersAndUptime)
{
    radio.tx_dropped_invalid_frame_ = 4294967295u;
    radio.tx_dropped_tx_queue_ = 4294967295u;
    radio.tx_dropped_wifi_ = 4294967295u;
    radio.tx_ether_pkt_ = 4294967295u;
    radio.tx_air_pkt_ = 4294967295u;
    device.uptime_us_ = 5000000000LL;
    EXPECT_EQ(
        run("tx_info"),
        "tx_info tx_queue_sz=3 in_flight=2 dropped_invalid_frame=4294967295 "
        "dropped_tx_queue=4294967295 dropped_wifi=4294967295 "
        "ether_pkt=4294967295 air_pkt=4294967295 ts=5000000000\n");
}

TEST_F(MplaneCommandsTest, RadioTxPartialUpdate)
{
    EXPECT_EQ(run("rt channel=6 cca=false"),
              "OK radio_tx channel=6 tx_power=20 modulation=DSS_1M_L "
              "cca=false\n");
    EXPECT_EQ(run("radio_tx modulation=OFDM_6M tx_power=13"),
              "OK radio_tx channel=6 tx_power=13 modulation=OFDM_6M "
              "cca=false\n");
}

TEST_F(MplaneCommandsTest, RadioTxInvalidArgument)
{
    EXPECT_EQ(run("rt channel=15"), "NOK EINVAL\n");
    EXPECT_EQ(run("rt tx_power=30"), "NOK EINVAL\n");
    EXPECT_EQ(run("rt cca=maybe"), "NOK EINVAL\n");
    EXPECT_EQ(run("rt modulation="), "NOK EINVAL\n");
    EXPECT_EQ(run("rt modulation=BOGUS"), "NOK EINVAL\n");
    EXPECT_EQ(run("rt power=3"), "NOK EINVAL\n");
    EXPECT_EQ(radio.set_radio_calls, 1);
}

TEST_F(MplaneCommandsTest, RadioTxInfoWithOptionalRssi)
{
    EXPECT_EQ(run("rti"),
              "radio_tx channel=1 tx_power=20 modulation=DSS_1M_L cca=true\n");
    radio.have_rssi = true;
    EXPECT_EQ(run("radio_tx_info"),
              "radio_tx channel=1 tx_power=20 modulation=DSS_1M_L cca=true\n"
              "radio_rx rssi=-42\n");
}

TEST_F(MplaneCommandsTest, RadioCapsInfo)
{
    EXPECT_EQ(run("radio_caps_info"), "OK radio_caps_info fcs=SIGNAL\n");
    EXPECT_EQ(run("rci"), "OK radio_caps_info fcs=SIGNAL\n");
    EXPECT_EQ(run("radio_caps_info x=1"), "NOK EINVAL\n");
    radio.fcs_mode_ = fcs_mode::actual;
    EXPECT_EQ(run("radio_caps_info"), "OK radio_caps_info fcs=ACTUAL\n");
    EXPECT_EQ(run("cmd:9 radio_caps_info"), "OK:9 radio_caps_info fcs=ACTUAL\n");
    mplane_commands ota(device, nullptr, nullptr);
    string_reply reply;
    std::string line = "radio_caps_info";
    ota.handle_line(line.data(), reply);
    EXPECT_EQ(reply.text, "NOK ENODEV\n");
}

TEST_F(MplaneCommandsTest, RxFilterAddr3SetQueryClear)
{
    EXPECT_EQ(run("rf3 addr=CA:FE:BA:BE:00:01"),
              "OK rx_filter_addr3 addr=ca:fe:ba:be:00:01\n");
    EXPECT_TRUE(radio.rx_filter_addr3().enabled);
    EXPECT_EQ(run("rf3"), "OK rx_filter_addr3 addr=ca:fe:ba:be:00:01\n");
    EXPECT_EQ(run("rx_filter_addr3 addr="), "OK rx_filter_addr3 addr=\n");
    EXPECT_FALSE(radio.rx_filter_addr3().enabled);
    EXPECT_EQ(run("rf3 addr=nope"), "NOK EINVAL\n");
}

TEST_F(MplaneCommandsTest, OtaModeRejectsRadioAndTestCommands)
{
    mplane_commands ota(device, nullptr, nullptr);
    string_reply reply;
    std::string a = "ti";
    ota.handle_line(a.data(), reply);
    std::string b = "test_wifi_rx_stat";
    ota.handle_line(b.data(), reply);
    std::string c = "ping";
    ota.handle_line(c.data(), reply);
    EXPECT_EQ(reply.text, "NOK ENODEV\nNOK ENODEV\npong\n");
}

TEST_F(MplaneCommandsTest, EtherRxPort)
{
    EXPECT_EQ(run("ter port=5001"), "OK ether_rx port=5001\n");
    EXPECT_EQ(test.ether_rx_port, 5001);
    EXPECT_EQ(run("test_ether_rx port=0"), "OK ether_rx port=0\n");
    EXPECT_EQ(test.ether_rx_port, 0);
    EXPECT_EQ(run("ter"), "NOK EINVAL\n");
}

TEST_F(MplaneCommandsTest, EtherTxStartAndStop)
{
    EXPECT_EQ(
        run("tet id=7 host=192.168.1.2 port=5001 mtu=1400 count=100 rate=8000"),
        "OK\n");
    EXPECT_EQ(test.ether_req.id, 7);
    EXPECT_EQ(test.ether_req.host, ip(192, 168, 1, 2));
    EXPECT_EQ(test.ether_req.port, 5001);
    EXPECT_EQ(test.ether_req.mtu, 1400);
    EXPECT_EQ(test.ether_req.count, 100);
    EXPECT_EQ(test.ether_req.rate_kbps, 8000u);

    EXPECT_EQ(run("tet id=7 count=0"), "OK\n");
    EXPECT_EQ(test.ether_req.count, 0);

    test.next_status = mplane_status::already;
    EXPECT_EQ(run("tet id=8 host=1.2.3.4 port=1 mtu=10 count=1"),
              "NOK EALREADY\n");
    test.next_status = mplane_status::stale;
    EXPECT_EQ(run("tet id=9 count=0"), "NOK ESTALE\n");
}

TEST_F(MplaneCommandsTest, EtherTxRejectsMissingOrBadFields)
{
    EXPECT_EQ(run("tet host=1.2.3.4 port=1 mtu=10 count=1"), "NOK EINVAL\n");
    EXPECT_EQ(run("tet id=1 port=1 mtu=10 count=1"), "NOK EINVAL\n");
    EXPECT_EQ(run("tet id=1 host=1.2.3.4 port=1 mtu=1473 count=1"),
              "NOK EINVAL\n");
    EXPECT_EQ(run("tet id=1 host=1.2.3.4 port=0 mtu=10 count=1"),
              "NOK EINVAL\n");
}

TEST_F(MplaneCommandsTest, RxStatsWithClear)
{
    EXPECT_EQ(run("ters"), "test_ether_rx_stat pkt=10 byt=14000\n");
    EXPECT_FALSE(test.last_clear);
    EXPECT_EQ(run("ters clear=true"), "test_ether_rx_stat pkt=10 byt=14000\n");
    EXPECT_TRUE(test.last_clear);
    EXPECT_EQ(run("twrs clear=1"),
              "test_wifi_rx_stat pkt=5 byt=500 fec_error_pkt=1\n");
    EXPECT_EQ(run("twrs clear=x"), "NOK EINVAL\n");
}

TEST_F(MplaneCommandsTest, WifiRxMatchAndStop)
{
    EXPECT_EQ(run("twr addr3=ca:fe:ba:be:00:01 addr1="), "OK\n");
    EXPECT_FALSE(test.wifi_match.addr1.enabled);
    EXPECT_FALSE(test.wifi_match.addr2.enabled);
    EXPECT_TRUE(test.wifi_match.addr3.enabled);
    EXPECT_TRUE(test.wifi_match.active());
    EXPECT_EQ(run("test_wifi_rx"), "OK\n");
    EXPECT_FALSE(test.wifi_match.active());
    EXPECT_EQ(run("twr addr2=xyz"), "NOK EINVAL\n");
}

TEST_F(MplaneCommandsTest, CmdPrefixCorrelation)
{
    EXPECT_EQ(run("cmd:3 ping"), "OK:3 pong\n");
    EXPECT_EQ(run("cmd:4 save 1"), "OK:4\n");
    EXPECT_EQ(run("cmd:5 radio_tx channel=6"),
              "OK:5 radio_tx channel=6 tx_power=20 modulation=DSS_1M_L "
              "cca=true\n");
    EXPECT_EQ(run("cmd:6 bogus"), "NOK:6 ENOSYS\n");
    EXPECT_EQ(run("cmd:x ping"), "NOK EINVAL\n");
    EXPECT_EQ(run("cmd:7"), "NOK EINVAL\n");
    EXPECT_EQ(run("ping"), "pong\n");
    EXPECT_EQ(run("cmd:8 reset id=9"), "OK:8 id=9\n");
}

TEST_F(MplaneCommandsTest, WifiTxStartAndStop)
{
    EXPECT_EQ(run("twt addr3=ca:fe:ba:be:00:01 mtu=200 count=50 rate=1000"),
              "OK\n");
    EXPECT_FALSE(test.wifi_req.id.has_value());
    EXPECT_FALSE(test.wifi_req.addr1.has_value());
    ASSERT_TRUE(test.wifi_req.addr3.has_value());
    EXPECT_EQ((*test.wifi_req.addr3)[5], 0x01);
    EXPECT_EQ(test.wifi_req.mtu, 200);
    EXPECT_EQ(test.wifi_req.count, 50);
    EXPECT_EQ(test.wifi_req.rate_kbps, 1000u);

    EXPECT_EQ(run("twt id=2 count=0"), "OK\n");
    ASSERT_TRUE(test.wifi_req.id.has_value());
    EXPECT_EQ(*test.wifi_req.id, 2);

    EXPECT_EQ(run("twt mtu=23 count=1"), "NOK EINVAL\n");
    EXPECT_EQ(run("twt mtu=100"), "NOK EINVAL\n");
}
