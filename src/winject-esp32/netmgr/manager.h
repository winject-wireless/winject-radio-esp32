#ifndef WINJECT_NETMGR_MANAGER_H_
#define WINJECT_NETMGR_MANAGER_H_

#include <atomic>
#include <stdint.h>

#include "esp_event.h"

#include "bfc-esp32/select_reactor.hpp"
#include "bfc-esp32/timer.hpp"
#include "config_types.h"
#include "freertos/semphr.h"

class dhcp_client;
class ethernet;

// Ethernet bring-up and IPv4 addressing. network() / set_network() must be
// called before start() or from the reactor task (they share the DHCP
// fallback timer with it).
class manager
{
public:
    using reactor_t = bfc::select_reactor<>;

    static manager& instance();
    manager(const manager&) = delete;
    manager& operator=(const manager&) = delete;
    ~manager();

    // Brings up Ethernet with the given EMAC DMA burst (beats), applies the
    // current network config, then runs the reactor task.
    bool start(uint8_t eth_dma_burst_beats);
    reactor_t& reactor();
    bool connected() const;
    bool link_up() const;

    network_config network() const;
    // Rejects invalid configs. Once Ethernet is up, re-applies the address:
    // static pins ip/prefix; dhcp (re)starts the client and re-arms the
    // fallback timer.
    bool set_network(const network_config& cfg);
    bool local_ipv4(uint32_t* out) const;

private:
    using timer_id_t = bfc::timer<>::timer_id_t;

    manager();
    static void reactor_task(void* arg);

    void run_reactor();
    void bring_up();
    void on_link_up();
    void on_static_fallback(uint32_t gen);
    static void eth_link_event_handler(void* arg, esp_event_base_t event_base,
                                       int32_t event_id, void* event_data);
    void schedule_static_fallback();
    void cancel_static_fallback();

    bool apply_network_locked();
    bool apply_network();

    ethernet& eth;
    class dhcp_client& dhcp_client;
    reactor_t reactor_;
    std::atomic<bool> started{false};
    std::atomic<bool> init_ok{false};
    network_config cfg_;
    std::atomic<uint32_t> auto_gen{0};
    timer_id_t fallback_timer_id{};
    bool fallback_timer_set = false;
    SemaphoreHandle_t init_done = nullptr;
    uint8_t eth_dma_burst_beats_ = 32;
    bool link_handler_registered_ = false;
};

#endif  // WINJECT_NETMGR_MANAGER_H_
