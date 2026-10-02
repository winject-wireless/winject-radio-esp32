#include "mplane_commands.h"

#include "config.h"
#include "mplane_args.h"
#include "mplane_reply.h"
#include "mplane_req_id.h"

#include <string.h>
#include <strings.h>

namespace
{
bool is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

char* trim(char* text)
{
    while (is_blank(*text))
    {
        ++text;
    }
    size_t n = strlen(text);
    while (n > 0 && is_blank(text[n - 1]))
    {
        text[--n] = '\0';
    }
    return text;
}

bool parse_args(char* text, const char* const* allowed, mplane_args* args)
{
    return args->parse(text) && args->only(allowed);
}

bool args_empty(const char* text)
{
    for (; *text != '\0'; ++text)
    {
        if (!is_blank(*text))
        {
            return false;
        }
    }
    return true;
}

const char* bool_name(bool v)
{
    return v ? "true" : "false";
}

// Parses an `addrN=` value: "" leaves the filter disabled.
bool parse_mac_filter(const char* value, mac_filter* out)
{
    *out = mac_filter{};
    if (value == nullptr || *value == '\0')
    {
        return true;
    }
    if (!mplane_parse_mac(value, out->addr))
    {
        return false;
    }
    out->enabled = true;
    return true;
}

bool parse_optional_mac(const char* value, std::optional<mac_address>* out)
{
    out->reset();
    if (value == nullptr || *value == '\0')
    {
        return true;
    }
    mac_address mac{};
    if (!mplane_parse_mac(value, mac.data()))
    {
        return false;
    }
    *out = mac;
    return true;
}
}  // namespace

const mplane_commands::command mplane_commands::k_commands[] = {
    {"help", "?", "", needs::device, &mplane_commands::cmd_help},
    {"ping", "p", "", needs::device, &mplane_commands::cmd_ping},
    {"reset", "r", "[id=<u8>] [mode=WINJECT|OTA]", needs::device,
     &mplane_commands::cmd_reset},
    {"save", nullptr, "<slot 0-9>  (network, radio, rx filter, tune)",
     needs::device, &mplane_commands::cmd_save},
    {"load", nullptr, "<slot 0-9>", needs::device, &mplane_commands::cmd_load},
    {"network", "sn",
     "ip=<a.b.c.d>/<prefix> type=<dhcp|static> timeout=<s>", needs::device,
     &mplane_commands::cmd_network},
    {"tune_param", "tp", "set_eth_dma_burst_len=<1|2|4|8|16|32>",
     needs::device, &mplane_commands::cmd_tune_param},
    {"tune_tx_param", "ttp",
     "eth_rx_ring_sz=<u8> tx_queue_sz=<u8> wifi_tx_ring_sz=<u8>",
     needs::device, &mplane_commands::cmd_tune_tx_param},
    {"tune_rx_param", "trp",
     "eth_tx_ring_sz=<u8> rx_queue_sz=<u8> wifi_rx_ring_sz=<u8>",
     needs::device, &mplane_commands::cmd_tune_rx_param},
    {"tx_info", "ti", "", needs::radio, &mplane_commands::cmd_tx_info},
    {"rx_info", "ri", "", needs::radio, &mplane_commands::cmd_rx_info},
    {"radio_tx", "rt",
     "channel=<u8> tx_power=<dBm> modulation=<name> cca=<bool>", needs::radio,
     &mplane_commands::cmd_radio_tx},
    {"radio_tx_info", "rti", "", needs::radio,
     &mplane_commands::cmd_radio_tx_info},
    {"radio_caps_info", "rci", "", needs::radio,
     &mplane_commands::cmd_radio_caps_info},
    {"rx_filter_addr3", "rf3", "addr=<mac|empty>", needs::radio,
     &mplane_commands::cmd_rx_filter_addr3},
    {"test_ether_rx", "ter", "port=<port|0>", needs::test,
     &mplane_commands::cmd_test_ether_rx},
    {"test_ether_tx", "tet",
     "id=<u8> host=<ip> port=<port> mtu=<u16> count=<u16|0> rate=<kbps>",
     needs::test, &mplane_commands::cmd_test_ether_tx},
    {"test_ether_rx_stat", "ters", "clear=<bool>", needs::test,
     &mplane_commands::cmd_test_ether_rx_stat},
    {"test_wifi_rx", "twr", "addr1=<mac> addr2=<mac> addr3=<mac>  (none: stop)",
     needs::test, &mplane_commands::cmd_test_wifi_rx},
    {"test_wifi_tx", "twt",
     "[id=<u8>] addr1=<mac> addr2=<mac> addr3=<mac> mtu=<u16> count=<u16|0> "
     "rate=<kbps>",
     needs::test, &mplane_commands::cmd_test_wifi_tx},
    {"test_wifi_rx_stat", "twrs", "clear=<bool>", needs::test,
     &mplane_commands::cmd_test_wifi_rx_stat},
};

mplane_commands::mplane_commands(mplane_device_backend& device,
                                 mplane_radio_backend* radio,
                                 mplane_test_backend* test)
    : device_(device), radio_(radio), test_(test)
{
}

void mplane_commands::handle_text(char* text, mplane_reply& reply)
{
    if (text == nullptr)
    {
        return;
    }
    char* line = text;
    while (line != nullptr)
    {
        char* next = strchr(line, '\n');
        if (next != nullptr)
        {
            *next++ = '\0';
        }
        handle_line(line, reply);
        line = next;
    }
}

void mplane_commands::handle_line(char* line, mplane_reply& reply)
{
    if (line == nullptr)
    {
        return;
    }
    char* text = trim(line);
    if (*text == '\0' || *text == '#')
    {
        return;
    }
    uint8_t req_id = 0;
    bool have_req_id = false;
    if (!mplane_peel_cmd_prefix(&text, &req_id, &have_req_id))
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    char* args = text;
    while (*args != '\0' && !is_blank(*args))
    {
        ++args;
    }
    if (*args != '\0')
    {
        *args++ = '\0';
    }
    mplane_req_id_reply req_reply(reply, req_id);
    mplane_reply& out = have_req_id ? static_cast<mplane_reply&>(req_reply)
                                    : reply;

    for (const command& cmd : k_commands)
    {
        if (strcasecmp(text, cmd.name) != 0 &&
            (cmd.alias == nullptr || strcasecmp(text, cmd.alias) != 0))
        {
            continue;
        }
        if ((cmd.backend == needs::radio && radio_ == nullptr) ||
            (cmd.backend == needs::test && test_ == nullptr))
        {
            out.nok(mplane_status::no_device);
            return;
        }
        (this->*cmd.fn)(args, out);
        return;
    }
    out.nok("ENOSYS");
}

void mplane_commands::cmd_help(char* args, mplane_reply& reply)
{
    (void)args;
    for (const command& cmd : k_commands)
    {
        if (cmd.alias != nullptr)
        {
            reply.print("%s|%s %s\n", cmd.name, cmd.alias, cmd.usage);
        }
        else
        {
            reply.print("%s %s\n", cmd.name, cmd.usage);
        }
    }
    if (radio_ != nullptr)
    {
        reply.write("modulations: ");
        reply.write(radio_->modulation_list());
        reply.write("\n");
    }
}

void mplane_commands::cmd_ping(char* args, mplane_reply& reply)
{
    if (!args_empty(args))
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    reply.write("pong\n");
}

void mplane_commands::cmd_reset(char* args, mplane_reply& reply)
{
    static const char* const k_keys[] = {"mode", "id", nullptr};
    mplane_args kv;
    if (!parse_args(args, k_keys, &kv))
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    std::optional<WinjectMode> mode;
    const char* value = kv.find("mode");
    if (value != nullptr)
    {
        if (strcasecmp(value, "WINJECT") == 0)
        {
            mode = WINJECT_MODE_STANDALONE;
        }
        else if (strcasecmp(value, "OTA") == 0)
        {
            mode = WINJECT_MODE_OTA;
        }
        else
        {
            reply.nok(mplane_status::invalid);
            return;
        }
    }
    std::optional<uint8_t> reset_id;
    const char* id_val = kv.find("id");
    if (id_val != nullptr)
    {
        unsigned long id = 0;
        if (!mplane_parse_uint(id_val, 0, 255, &id))
        {
            reply.nok(mplane_status::invalid);
            return;
        }
        reset_id = static_cast<uint8_t>(id);
    }
    if (reset_id.has_value())
    {
        const mplane_status id_st = device_.accept_reset_id(*reset_id);
        if (id_st != mplane_status::ok)
        {
            reply.nok(id_st);
            return;
        }
    }
    const mplane_status st = device_.restart(mode);
    if (st != mplane_status::ok)
    {
        reply.nok(st);
        return;
    }
    if (reset_id.has_value())
    {
        reply.print("OK id=%u\n", static_cast<unsigned>(*reset_id));
    }
    else
    {
        reply.ok();
    }
}

void mplane_commands::handle_slot(char* args, bool save, mplane_reply& reply)
{
    unsigned long slot = 0;
    if (!mplane_parse_uint(trim(args), 0, SETTINGS_SLOT_COUNT - 1, &slot))
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    const uint8_t s = static_cast<uint8_t>(slot);
    const mplane_status st = save ? device_.save(s) : device_.load(s);
    if (st != mplane_status::ok)
    {
        reply.nok(st);
        return;
    }
    reply.ok();
}

void mplane_commands::cmd_save(char* args, mplane_reply& reply)
{
    handle_slot(args, true, reply);
}

void mplane_commands::cmd_load(char* args, mplane_reply& reply)
{
    handle_slot(args, false, reply);
}

void mplane_commands::cmd_network(char* args, mplane_reply& reply)
{
    static const char* const k_keys[] = {"ip", "type", "timeout", nullptr};
    mplane_args kv;
    if (!parse_args(args, k_keys, &kv))
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    network_config cfg = device_.network();
    if (!kv.empty())
    {
        const char* ip = kv.find("ip");
        if (ip != nullptr)
        {
            bool has_prefix = false;
            if (!mplane_parse_ipv4_prefix(ip, &cfg.ip, &cfg.prefix,
                                          &has_prefix))
            {
                reply.nok(mplane_status::invalid);
                return;
            }
        }
        const char* type = kv.find("type");
        if (type != nullptr)
        {
            if (strcasecmp(type, "dhcp") == 0)
            {
                cfg.type = network_type::dhcp;
            }
            else if (strcasecmp(type, "static") == 0)
            {
                cfg.type = network_type::static_ip;
            }
            else
            {
                reply.nok(mplane_status::invalid);
                return;
            }
        }
        unsigned long timeout = 0;
        const mplane_args::status ts = kv.get_uint("timeout", 0, 65535, &timeout);
        if (ts == mplane_args::status::invalid)
        {
            reply.nok(mplane_status::invalid);
            return;
        }
        if (ts == mplane_args::status::ok)
        {
            cfg.timeout_s = static_cast<uint16_t>(timeout);
        }
        if (!network_config_valid(cfg))
        {
            reply.nok(mplane_status::invalid);
            return;
        }
        const mplane_status st = device_.set_network(cfg);
        if (st != mplane_status::ok)
        {
            reply.nok(st);
            return;
        }
        cfg = device_.network();
    }
    char ip_str[16];
    mplane_format_ipv4(cfg.ip, ip_str, sizeof(ip_str));
    reply.print("OK network ip=%s/%u type=%s timeout=%u\n", ip_str,
                static_cast<unsigned>(cfg.prefix),
                cfg.type == network_type::dhcp ? "dhcp" : "static",
                static_cast<unsigned>(cfg.timeout_s));
}

void mplane_commands::handle_tune(char* args, const char* name,
                                  const tune_field* fields, size_t field_count,
                                  mplane_reply& reply)
{
    static constexpr size_t k_max_fields = 3;
    const char* keys[k_max_fields + 1] = {};
    for (size_t i = 0; i < field_count && i < k_max_fields; ++i)
    {
        keys[i] = fields[i].key;
    }
    mplane_args kv;
    if (field_count > k_max_fields || !parse_args(args, keys, &kv))
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    tune_config cfg = device_.tune();
    if (!kv.empty())
    {
        for (size_t i = 0; i < field_count; ++i)
        {
            unsigned long v = 0;
            const mplane_args::status s = kv.get_uint(fields[i].key, 0, 255, &v);
            if (s == mplane_args::status::invalid)
            {
                reply.nok(mplane_status::invalid);
                return;
            }
            if (s == mplane_args::status::ok)
            {
                cfg.*(fields[i].member) = static_cast<uint8_t>(v);
            }
        }
        const mplane_status st = device_.set_tune(cfg);
        if (st != mplane_status::ok)
        {
            reply.nok(st);
            return;
        }
        cfg = device_.tune();
    }
    reply.print("OK %s", name);
    for (size_t i = 0; i < field_count; ++i)
    {
        reply.print(" %s=%u", fields[i].key,
                    static_cast<unsigned>(cfg.*(fields[i].member)));
    }
    reply.write("\n");
}

void mplane_commands::cmd_tune_param(char* args, mplane_reply& reply)
{
    static const tune_field k_fields[] = {
        {"set_eth_dma_burst_len", &tune_config::eth_dma_burst_len},
    };
    handle_tune(args, "tune_param", k_fields, 1, reply);
}

void mplane_commands::cmd_tune_tx_param(char* args, mplane_reply& reply)
{
    static const tune_field k_fields[] = {
        {"eth_rx_ring_sz", &tune_config::eth_rx_ring_sz},
        {"tx_queue_sz", &tune_config::tx_queue_sz},
        {"wifi_tx_ring_sz", &tune_config::wifi_tx_ring_sz},
    };
    handle_tune(args, "tune_tx_param", k_fields, 3, reply);
}

void mplane_commands::cmd_tune_rx_param(char* args, mplane_reply& reply)
{
    static const tune_field k_fields[] = {
        {"eth_tx_ring_sz", &tune_config::eth_tx_ring_sz},
        {"rx_queue_sz", &tune_config::rx_queue_sz},
        {"wifi_rx_ring_sz", &tune_config::wifi_rx_ring_sz},
    };
    handle_tune(args, "tune_rx_param", k_fields, 3, reply);
}

void mplane_commands::cmd_tx_info(char* args, mplane_reply& reply)
{
    if (!args_empty(args))
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    reply.print(
        "tx_info tx_queue_sz=%u in_flight=%u dropped_invalid_frame=%lu "
        "dropped_tx_queue=%lu dropped_wifi=%lu ether_pkt=%lu air_pkt=%lu "
        "ts=%llu\n",
        static_cast<unsigned>(radio_->tx_queue_size()),
        static_cast<unsigned>(radio_->tx_in_flight()),
        static_cast<unsigned long>(radio_->tx_dropped_invalid_frame()),
        static_cast<unsigned long>(radio_->tx_dropped_tx_queue()),
        static_cast<unsigned long>(radio_->tx_dropped_wifi()),
        static_cast<unsigned long>(radio_->tx_ether_pkt()),
        static_cast<unsigned long>(radio_->tx_air_pkt()),
        static_cast<unsigned long long>(device_.uptime_us()));
}

void mplane_commands::cmd_rx_info(char* args, mplane_reply& reply)
{
    if (!args_empty(args))
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    reply.print(
        "rx_info rx_queue_sz=%u dropped_filter_mismatched=%lu "
        "dropped_rx_queue=%lu dropped_no_peer=%lu dropped_send_failed=%lu "
        "ether_pkt=%lu air_pkt=%lu ts=%llu\n",
        static_cast<unsigned>(radio_->rx_queue_size()),
        static_cast<unsigned long>(radio_->rx_dropped_filter_mismatched()),
        static_cast<unsigned long>(radio_->rx_dropped_rx_queue()),
        static_cast<unsigned long>(radio_->rx_dropped_no_peer()),
        static_cast<unsigned long>(radio_->rx_dropped_send_failed()),
        static_cast<unsigned long>(radio_->rx_ether_pkt()),
        static_cast<unsigned long>(radio_->rx_air_pkt()),
        static_cast<unsigned long long>(device_.uptime_us()));
}

void mplane_commands::print_radio(const char* prefix, const radio_config& cfg,
                                  mplane_reply& reply)
{
    reply.print("%sradio_tx channel=%u tx_power=%d modulation=%s cca=%s\n",
                prefix, static_cast<unsigned>(cfg.channel),
                static_cast<int>(cfg.tx_power_dbm), cfg.modulation,
                bool_name(cfg.cca_enabled));
}

void mplane_commands::cmd_radio_tx(char* args, mplane_reply& reply)
{
    static const char* const k_keys[] = {"channel", "tx_power", "modulation",
                                         "cca", nullptr};
    mplane_args kv;
    if (!parse_args(args, k_keys, &kv))
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    if (!kv.empty())
    {
        radio_patch patch;
        unsigned long channel = 0;
        long power = 0;
        bool cca = false;
        const mplane_args::status cs =
            kv.get_uint("channel", WIFI_CHANNEL_MIN, WIFI_CHANNEL_MAX, &channel);
        const mplane_args::status ps = kv.get_int(
            "tx_power", WIFI_TX_POWER_DBM_MIN, WIFI_TX_POWER_DBM_MAX, &power);
        const mplane_args::status bs = kv.get_bool("cca", &cca);
        const char* modulation = kv.find("modulation");
        if (cs == mplane_args::status::invalid ||
            ps == mplane_args::status::invalid ||
            bs == mplane_args::status::invalid ||
            (modulation != nullptr &&
             (*modulation == '\0' ||
              strlen(modulation) >= SETTINGS_MODULATION_MAX)))
        {
            reply.nok(mplane_status::invalid);
            return;
        }
        if (cs == mplane_args::status::ok)
        {
            patch.channel = static_cast<uint8_t>(channel);
        }
        if (ps == mplane_args::status::ok)
        {
            patch.tx_power_dbm = static_cast<int8_t>(power);
        }
        if (bs == mplane_args::status::ok)
        {
            patch.cca_enabled = cca;
        }
        patch.modulation = modulation;
        const mplane_status st = radio_->set_radio(patch);
        if (st != mplane_status::ok)
        {
            reply.nok(st);
            return;
        }
    }
    print_radio("OK ", radio_->radio(), reply);
}

void mplane_commands::cmd_radio_tx_info(char* args, mplane_reply& reply)
{
    if (!args_empty(args))
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    print_radio("", radio_->radio(), reply);
    int8_t rssi = 0;
    if (radio_->rx_rssi(&rssi))
    {
        reply.print("radio_rx rssi=%d\n", static_cast<int>(rssi));
    }
}

namespace
{
const char* fcs_mode_name(fcs_mode mode)
{
    switch (mode)
    {
    case fcs_mode::signal:
        return "SIGNAL";
    case fcs_mode::actual:
        return "ACTUAL";
    }
    return "ACTUAL";
}
}  // namespace

void mplane_commands::cmd_radio_caps_info(char* args, mplane_reply& reply)
{
    if (!args_empty(args))
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    reply.print("OK radio_caps_info fcs=%s\n",
                fcs_mode_name(radio_->caps().fcs));
}

void mplane_commands::cmd_rx_filter_addr3(char* args, mplane_reply& reply)
{
    static const char* const k_keys[] = {"addr", nullptr};
    mplane_args kv;
    if (!parse_args(args, k_keys, &kv))
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    if (!kv.empty())
    {
        mac_filter filter;
        if (!parse_mac_filter(kv.find("addr"), &filter))
        {
            reply.nok(mplane_status::invalid);
            return;
        }
        const mplane_status st = radio_->set_rx_filter_addr3(filter);
        if (st != mplane_status::ok)
        {
            reply.nok(st);
            return;
        }
    }
    const mac_filter cur = radio_->rx_filter_addr3();
    char mac[18] = "";
    if (cur.enabled)
    {
        mplane_format_mac(cur.addr, mac, sizeof(mac));
    }
    reply.print("OK rx_filter_addr3 addr=%s\n", mac);
}

void mplane_commands::cmd_test_ether_rx(char* args, mplane_reply& reply)
{
    static const char* const k_keys[] = {"port", nullptr};
    mplane_args kv;
    unsigned long port = 0;
    if (!parse_args(args, k_keys, &kv) ||
        kv.get_uint("port", 0, 65535, &port) != mplane_args::status::ok)
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    const mplane_status st =
        test_->set_ether_rx_port(static_cast<uint16_t>(port));
    if (st != mplane_status::ok)
    {
        reply.nok(st);
        return;
    }
    reply.print("OK ether_rx port=%lu\n", port);
}

void mplane_commands::cmd_test_ether_tx(char* args, mplane_reply& reply)
{
    static const char* const k_keys[] = {"id",  "host",  "port", "mtu",
                                         "count", "rate", nullptr};
    mplane_args kv;
    unsigned long id = 0;
    unsigned long count = 0;
    if (!parse_args(args, k_keys, &kv) ||
        kv.get_uint("id", 0, 255, &id) != mplane_args::status::ok ||
        kv.get_uint("count", 0, 65535, &count) != mplane_args::status::ok)
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    ether_tx_request req;
    req.id = static_cast<uint8_t>(id);
    req.count = static_cast<uint16_t>(count);
    if (count != 0)
    {
        unsigned long port = 0;
        unsigned long mtu = 0;
        unsigned long rate = 0;
        if (!mplane_parse_ipv4(kv.find("host"), &req.host) ||
            kv.get_uint("port", 1, 65535, &port) != mplane_args::status::ok ||
            kv.get_uint("mtu", 1, ETHER_TEST_MTU_MAX, &mtu) !=
                mplane_args::status::ok ||
            kv.get_uint("rate", 0, 1000000, &rate) ==
                mplane_args::status::invalid)
        {
            reply.nok(mplane_status::invalid);
            return;
        }
        req.port = static_cast<uint16_t>(port);
        req.mtu = static_cast<uint16_t>(mtu);
        req.rate_kbps = static_cast<uint32_t>(rate);
    }
    const mplane_status st = test_->ether_tx(req);
    if (st != mplane_status::ok)
    {
        reply.nok(st);
        return;
    }
    reply.ok();
}

bool mplane_commands::parse_clear(char* args, bool* clear)
{
    static const char* const k_keys[] = {"clear", nullptr};
    mplane_args kv;
    *clear = false;
    return parse_args(args, k_keys, &kv) &&
           kv.get_bool("clear", clear) != mplane_args::status::invalid;
}

void mplane_commands::print_stats(const char* name, const rx_test_stats& stats,
                                  bool with_fec, mplane_reply& reply)
{
    reply.print("%s pkt=%llu byt=%llu", name,
                static_cast<unsigned long long>(stats.pkt),
                static_cast<unsigned long long>(stats.byt));
    if (with_fec)
    {
        reply.print(" fec_error_pkt=%llu",
                    static_cast<unsigned long long>(stats.fec_error_pkt));
    }
    reply.write("\n");
}

void mplane_commands::cmd_test_ether_rx_stat(char* args, mplane_reply& reply)
{
    bool clear = false;
    if (!parse_clear(args, &clear))
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    print_stats("test_ether_rx_stat", test_->ether_rx_stats(clear), false,
                reply);
}

void mplane_commands::cmd_test_wifi_rx(char* args, mplane_reply& reply)
{
    static const char* const k_keys[] = {"addr1", "addr2", "addr3", nullptr};
    mplane_args kv;
    wifi_rx_match match;
    if (!parse_args(args, k_keys, &kv) ||
        !parse_mac_filter(kv.find("addr1"), &match.addr1) ||
        !parse_mac_filter(kv.find("addr2"), &match.addr2) ||
        !parse_mac_filter(kv.find("addr3"), &match.addr3))
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    const mplane_status st = test_->set_wifi_rx_match(match);
    if (st != mplane_status::ok)
    {
        reply.nok(st);
        return;
    }
    reply.ok();
}

void mplane_commands::cmd_test_wifi_tx(char* args, mplane_reply& reply)
{
    static const char* const k_keys[] = {"id",  "addr1", "addr2", "addr3",
                                         "mtu", "count", "rate",  nullptr};
    mplane_args kv;
    unsigned long count = 0;
    if (!parse_args(args, k_keys, &kv) ||
        kv.get_uint("count", 0, 65535, &count) != mplane_args::status::ok)
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    wifi_tx_request req;
    req.count = static_cast<uint16_t>(count);
    unsigned long id = 0;
    const mplane_args::status ids = kv.get_uint("id", 0, 255, &id);
    if (ids == mplane_args::status::invalid)
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    if (ids == mplane_args::status::ok)
    {
        req.id = static_cast<uint8_t>(id);
    }
    if (count != 0)
    {
        unsigned long mtu = 0;
        unsigned long rate = 0;
        if (kv.get_uint("mtu", WIFI_RADIO_INJECT_MIN, WIFI_RADIO_INJECT_MAX,
                        &mtu) != mplane_args::status::ok ||
            kv.get_uint("rate", 0, 1000000, &rate) ==
                mplane_args::status::invalid ||
            !parse_optional_mac(kv.find("addr1"), &req.addr1) ||
            !parse_optional_mac(kv.find("addr2"), &req.addr2) ||
            !parse_optional_mac(kv.find("addr3"), &req.addr3))
        {
            reply.nok(mplane_status::invalid);
            return;
        }
        req.mtu = static_cast<uint16_t>(mtu);
        req.rate_kbps = static_cast<uint32_t>(rate);
    }
    const mplane_status st = test_->wifi_tx(req);
    if (st != mplane_status::ok)
    {
        reply.nok(st);
        return;
    }
    reply.ok();
}

void mplane_commands::cmd_test_wifi_rx_stat(char* args, mplane_reply& reply)
{
    bool clear = false;
    if (!parse_clear(args, &clear))
    {
        reply.nok(mplane_status::invalid);
        return;
    }
    print_stats("test_wifi_rx_stat", test_->wifi_rx_stats(clear), true, reply);
}
