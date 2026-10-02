#include "console.h"

#include "config.h"
#include "manager.h"
#include "mplane_commands.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char* TAG = "console";

namespace
{
bool set_nonblock(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void ipv4_to_string(uint32_t addr, char* out, size_t out_len)
{
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&addr);
    snprintf(out, out_len, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}
}  // namespace

void console::stop_udp()
{
    if (sock_fd < 0)
    {
        return;
    }
    if (reactor != nullptr)
    {
        reactor->rem_read_rdy(sock_fd);
    }
    close(sock_fd);
    sock_fd = -1;
    reply_to_set = false;
    reply_len = 0;
}

void console::write(const char* data, size_t n)
{
    if (data == nullptr || n == 0 || sock_fd < 0 || !reply_to_set)
    {
        return;
    }
    size_t off = 0;
    while (off < n)
    {
        if (reply_len >= k_reply_max)
        {
            flush_reply();
        }
        const size_t room = k_reply_max - reply_len;
        const size_t chunk = n - off < room ? n - off : room;
        memcpy(reply + reply_len, data + off, chunk);
        reply_len += chunk;
        off += chunk;
    }
}

void console::flush_reply()
{
    if (sock_fd < 0 || !reply_to_set || reply_len == 0)
    {
        reply_len = 0;
        return;
    }
    const int64_t deadline_us = esp_timer_get_time() + k_send_timeout_us;
    while (true)
    {
        const int sent = sendto(sock_fd, reply, reply_len, 0,
                                reinterpret_cast<struct sockaddr*>(&reply_to),
                                sizeof(reply_to));
        if (sent >= 0)
        {
            break;
        }
        if (errno == EINTR)
        {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            if (esp_timer_get_time() >= deadline_us)
            {
                ESP_LOGW(TAG, "udp send timeout (%u bytes)",
                         static_cast<unsigned>(reply_len));
                break;
            }
            vTaskDelay(1);
            continue;
        }
        ESP_LOGW(TAG, "udp send of %u bytes failed: %d",
                 static_cast<unsigned>(reply_len), errno);
        break;
    }
    reply_len = 0;
}

void console::start_udp()
{
    if (sock_fd >= 0)
    {
        return;
    }

    const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0)
    {
        ESP_LOGE(TAG, "socket failed: %d", errno);
        return;
    }

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    const int rcvbuf = 48 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    const int sndbuf = static_cast<int>(k_reply_max);
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CONTROL_CONSOLE_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        ESP_LOGE(TAG, "bind *:%u failed: %d", CONTROL_CONSOLE_PORT, errno);
        close(fd);
        return;
    }
    if (!set_nonblock(fd))
    {
        ESP_LOGE(TAG, "udp nonblock failed: %d", errno);
        close(fd);
        return;
    }

    sock_fd = fd;
    reactor->add_read_rdy(sock_fd,
                          [this]()
                          {
                              on_datagram();
                          });

    uint32_t ip = 0;
    if (netmgr->local_ipv4(&ip))
    {
        char ip_str[16];
        ipv4_to_string(ip, ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "m-plane udp %s:%u", ip_str, CONTROL_CONSOLE_PORT);
    }
}

void console::on_datagram()
{
    while (sock_fd >= 0)
    {
        sockaddr_in peer = {};
        socklen_t peer_len = sizeof(peer);
        const int n =
            recvfrom(sock_fd, recv_buf, sizeof(recv_buf) - 1, 0,
                     reinterpret_cast<struct sockaddr*>(&peer), &peer_len);
        if (n < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            {
                ESP_LOGW(TAG, "udp recv failed: %d", errno);
            }
            return;
        }
        if (n == 0)
        {
            continue;
        }
        recv_buf[n] = '\0';
        reply_to = peer;
        reply_to_set = true;
        reply_len = 0;
        commands_->handle_text(recv_buf, *this);
        flush_reply();
        reply_to_set = false;
    }
}

void console::sync_udp()
{
    if (netmgr->connected())
    {
        start_udp();
    }
    else
    {
        stop_udp();
    }
}

void console::schedule_sync()
{
    reactor->get_timer().wait_ms(k_sync_ms,
                                 [this]()
                                 {
                                     sync_udp();
                                     schedule_sync();
                                 });
}

void console::attach_reactor()
{
    schedule_sync();
    sync_udp();
}

bool console::init(manager& netmgr, mplane_commands& commands)
{
    if (ready)
    {
        return true;
    }
    this->netmgr = &netmgr;
    commands_ = &commands;
    reactor = &netmgr.reactor();
    if (!reactor->wake_up(
            [this]()
            {
                attach_reactor();
            }))
    {
        ESP_LOGE(TAG, "console init: wake_up failed");
        return false;
    }
    ready = true;
    return true;
}
