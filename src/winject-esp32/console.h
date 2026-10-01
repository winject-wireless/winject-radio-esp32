#ifndef WINJECT_CONSOLE_H_
#define WINJECT_CONSOLE_H_

#include "mplane_reply.h"

#include <stddef.h>
#include <stdint.h>

#include "bfc-esp32/select_reactor.hpp"
#include "lwip/sockets.h"

class manager;
class mplane_commands;

// UDP m-plane transport on CONTROL_CONSOLE_PORT, run on the netmgr reactor.
// Each datagram holds one or more command lines; their replies go back to the
// sender as one datagram.
class console : private mplane_reply
{
public:
    console() = default;
    console(const console&) = delete;
    console& operator=(const console&) = delete;

    // commands must outlive the console.
    bool init(manager& netmgr, mplane_commands& commands);

private:
    using reactor_t = bfc::select_reactor<>;

    static constexpr int k_sync_ms = 250;
    // One Ethernet MTU UDP datagram (IP+UDP headers leave ~1472).
    static constexpr size_t k_datagram_max = 1500;
    static constexpr size_t k_reply_max = 16384;
    static constexpr int64_t k_send_timeout_us = 10 * 1000;

    void write(const char* data, size_t n) override;

    void attach_reactor();
    void schedule_sync();
    void sync_udp();
    void start_udp();
    void stop_udp();
    void on_datagram();
    void flush_reply();

    manager* netmgr = nullptr;
    mplane_commands* commands_ = nullptr;
    reactor_t* reactor = nullptr;
    bool ready = false;
    int sock_fd = -1;
    sockaddr_in reply_to{};
    bool reply_to_set = false;
    char reply[k_reply_max]{};
    size_t reply_len = 0;
    char recv_buf[k_datagram_max]{};
};

#endif  // WINJECT_CONSOLE_H_
