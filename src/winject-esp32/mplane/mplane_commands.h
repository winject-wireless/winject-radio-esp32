#ifndef WINJECT_MPLANE_MPLANE_COMMANDS_H_
#define WINJECT_MPLANE_MPLANE_COMMANDS_H_

#include "mplane_backend.h"

#include <stddef.h>
#include <stdint.h>

class mplane_args;
class mplane_reply;

// M-plane command set (docs/mplane.md): parses one text line, calls the
// backends, and formats the reply. Not thread-safe; call from one task.
class mplane_commands
{
public:
    // radio / test are nullptr when unavailable (OTA mode); their commands
    // then reply `NOK ENODEV`. Backends must outlive this object.
    mplane_commands(mplane_device_backend& device, mplane_radio_backend* radio,
                    mplane_test_backend* test);

    mplane_commands(const mplane_commands&) = delete;
    mplane_commands& operator=(const mplane_commands&) = delete;

    // Runs every newline-separated command in text (modified in place).
    void handle_text(char* text, mplane_reply& reply);
    // Runs one command line (no newline; modified in place).
    void handle_line(char* line, mplane_reply& reply);

private:
    enum class needs : uint8_t
    {
        device,
        radio,
        test,
    };

    using handler = void (mplane_commands::*)(char* args, mplane_reply& reply);

    struct command
    {
        const char* name;
        const char* alias;
        const char* usage;
        needs backend;
        handler fn;
    };

    struct tune_field
    {
        const char* key;
        uint8_t tune_config::*member;
    };

    static const command k_commands[];

    void cmd_help(char* args, mplane_reply& reply);
    void cmd_ping(char* args, mplane_reply& reply);
    void cmd_version(char* args, mplane_reply& reply);
    void cmd_reset(char* args, mplane_reply& reply);
    void cmd_save(char* args, mplane_reply& reply);
    void cmd_load(char* args, mplane_reply& reply);
    void cmd_network(char* args, mplane_reply& reply);
    void cmd_tune_param(char* args, mplane_reply& reply);
    void cmd_tune_tx_param(char* args, mplane_reply& reply);
    void cmd_tune_rx_param(char* args, mplane_reply& reply);
    void cmd_tx_info(char* args, mplane_reply& reply);
    void cmd_rx_info(char* args, mplane_reply& reply);
    void cmd_radio_tx(char* args, mplane_reply& reply);
    void cmd_radio_tx_info(char* args, mplane_reply& reply);
    void cmd_radio_caps_info(char* args, mplane_reply& reply);
    void cmd_rx_filter_addr3(char* args, mplane_reply& reply);
    void cmd_test_ether_rx(char* args, mplane_reply& reply);
    void cmd_test_ether_tx(char* args, mplane_reply& reply);
    void cmd_test_ether_rx_stat(char* args, mplane_reply& reply);
    void cmd_test_wifi_rx(char* args, mplane_reply& reply);
    void cmd_test_wifi_tx(char* args, mplane_reply& reply);
    void cmd_test_wifi_rx_stat(char* args, mplane_reply& reply);

    void handle_tune(char* args, const char* name, const tune_field* fields,
                     size_t field_count, mplane_reply& reply);
    void handle_slot(char* args, bool save, mplane_reply& reply);
    static void print_radio(const char* prefix, const radio_config& cfg,
                            mplane_reply& reply);
    static void print_stats(const char* name, const rx_test_stats& stats,
                            bool with_fec, mplane_reply& reply);
    static bool parse_clear(char* args, bool* clear);

    mplane_device_backend& device_;
    mplane_radio_backend* radio_;
    mplane_test_backend* test_;
};

#endif  // WINJECT_MPLANE_MPLANE_COMMANDS_H_
