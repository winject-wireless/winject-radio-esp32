#include "manager.h"

#include "config.h"
#include "dhcp_client.h"
#include "ethernet.h"

#include "upstream_tx_endpoint.h"

#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char* TAG = "netmgr";

manager& manager::instance()
{
    static manager inst;
    return inst;
}

manager::manager()
    : eth(ethernet::instance()), dhcp_client(dhcp_client::instance())
{
    esp_ip4_addr_t addr = {};
    esp_netif_set_ip4_addr(&addr, ETH_FALLBACK_ADDR);
    cfg_.ip = addr.addr;
}

manager::~manager()
{
    if (started.load(std::memory_order_acquire))
    {
        reactor_.stop();
    }
}

bool manager::apply_network_locked()
{
    if (!eth.ready())
    {
        return false;
    }

    esp_netif_t* netif = eth.netif();
    auto_gen.fetch_add(1, std::memory_order_relaxed);
    if (cfg_.type == network_type::dhcp)
    {
        uint32_t ip = 0;
        if (eth.has_ipv4() && eth.local_ipv4(&ip))
        {
            upstream_tx_endpoint::instance().set_local_ipv4(ip);
        }
        else
        {
            upstream_tx_endpoint::instance().set_local_ipv4(0);
        }
        if (!dhcp_client.start(netif))
        {
            return false;
        }
        eth.set_using_static(false);
        eth.set_connected(eth.has_ipv4());
        ESP_LOGI(TAG, "network dhcp (fallback after %u s)",
                 static_cast<unsigned>(cfg_.timeout_s));
        schedule_static_fallback();
        return true;
    }

    cancel_static_fallback();
    return eth.apply_static_ip(cfg_.ip, network_prefix_netmask(cfg_.prefix));
}

bool manager::apply_network()
{
    if (!eth.mutex().ready())
    {
        return true;
    }
    bfc::semaphore::lock lock(eth.mutex());
    if (!lock)
    {
        ESP_LOGE(TAG, "network apply: netif lock failed");
        return false;
    }
    return apply_network_locked();
}

void manager::cancel_static_fallback()
{
    if (!fallback_timer_set)
    {
        return;
    }
    reactor_.get_timer().cancel(fallback_timer_id);
    fallback_timer_set = false;
}

void manager::on_static_fallback(uint32_t gen)
{
    fallback_timer_set = false;
    if (auto_gen.load(std::memory_order_relaxed) != gen ||
        cfg_.type != network_type::dhcp)
    {
        return;
    }

    bfc::semaphore::lock lock(eth.mutex());
    if (!lock)
    {
        ESP_LOGE(TAG, "static fallback lock failed");
        return;
    }
    if (auto_gen.load(std::memory_order_relaxed) != gen || eth.has_ipv4())
    {
        return;
    }
    ESP_LOGI(TAG, "no DHCP lease after %u s; static fallback",
             static_cast<unsigned>(cfg_.timeout_s));
    if (!eth.apply_static_ip(cfg_.ip, network_prefix_netmask(cfg_.prefix)))
    {
        ESP_LOGE(TAG, "static fallback failed");
    }
}

void manager::schedule_static_fallback()
{
    cancel_static_fallback();
    if (cfg_.timeout_s == 0)
    {
        return;
    }
    const uint32_t gen = auto_gen.load(std::memory_order_relaxed);
    fallback_timer_id = reactor_.get_timer().wait_ms(
        static_cast<uint32_t>(cfg_.timeout_s) * 1000u,
        [this, gen]()
        {
            on_static_fallback(gen);
        });
    fallback_timer_set = true;
}

void manager::on_link_up()
{
    if (cfg_.type != network_type::dhcp || !eth.using_static())
    {
        return;
    }
    bfc::semaphore::lock lock(eth.mutex());
    if (!lock)
    {
        ESP_LOGE(TAG, "link up: netif lock failed");
        return;
    }
    if (!apply_network_locked())
    {
        ESP_LOGE(TAG, "link up: dhcp re-arm failed");
    }
}

void manager::eth_link_event_handler(void* arg, esp_event_base_t event_base,
                                     int32_t event_id, void* event_data)
{
    (void)event_base;
    (void)event_data;
    auto* self = static_cast<manager*>(arg);
    if (self == nullptr || event_id != ETHERNET_EVENT_CONNECTED)
    {
        return;
    }
    self->reactor_.wake_up(
        [self]()
        {
            self->on_link_up();
        });
}

void manager::bring_up()
{
    if (!eth.begin(eth_dma_burst_beats_))
    {
        ESP_LOGE(TAG, "ethernet begin failed");
        init_ok.store(false, std::memory_order_release);
        return;
    }
    if (!link_handler_registered_)
    {
        if (esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                       &eth_link_event_handler, this) == ESP_OK)
        {
            link_handler_registered_ = true;
        }
        else
        {
            ESP_LOGE(TAG, "link event register failed");
            init_ok.store(false, std::memory_order_release);
            return;
        }
    }
    if (!apply_network())
    {
        ESP_LOGE(TAG, "network apply failed");
        init_ok.store(false, std::memory_order_release);
        return;
    }
    init_ok.store(true, std::memory_order_release);
}

void manager::run_reactor()
{
    bring_up();
    if (!reactor_.ensure_wake())
    {
        ESP_LOGE(TAG, "reactor wake socket failed");
        init_ok.store(false, std::memory_order_release);
    }
    SemaphoreHandle_t done = init_done;
    if (done != nullptr)
    {
        xSemaphoreGive(done);
    }
    reactor_.run();
}

void manager::reactor_task(void* arg)
{
    static_cast<manager*>(arg)->run_reactor();
}

bool manager::start(uint8_t eth_dma_burst_beats)
{
    if (started.load(std::memory_order_acquire))
    {
        return init_ok.load(std::memory_order_acquire);
    }
    eth_dma_burst_beats_ = eth_dma_burst_beats;

    SemaphoreHandle_t init_done = xSemaphoreCreateBinary();
    if (init_done == nullptr)
    {
        ESP_LOGE(TAG, "init semaphore alloc failed");
        return false;
    }
    this->init_done = init_done;

    const BaseType_t ok =
        xTaskCreatePinnedToCore(reactor_task, "reactor", 6144, this,
                                NETMGR_TASK_PRIO, nullptr, APP_TASK_CORE);
    if (ok != pdPASS)
    {
        ESP_LOGE(TAG, "reactor task create failed");
        vSemaphoreDelete(init_done);
        this->init_done = nullptr;
        return false;
    }

    if (xSemaphoreTake(init_done, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "reactor init wait failed");
        vSemaphoreDelete(init_done);
        this->init_done = nullptr;
        reactor_.stop();
        return false;
    }
    vSemaphoreDelete(init_done);
    this->init_done = nullptr;

    started.store(true, std::memory_order_release);
    return init_ok.load(std::memory_order_acquire);
}

manager::reactor_t& manager::reactor()
{
    return reactor_;
}

bool manager::connected() const
{
    return eth.connected();
}

bool manager::link_up() const
{
    return eth.link_up();
}

network_config manager::network() const
{
    return cfg_;
}

bool manager::set_network(const network_config& cfg)
{
    if (!network_config_valid(cfg))
    {
        return false;
    }
    cfg_ = cfg;
    return apply_network();
}

bool manager::local_ipv4(uint32_t* out) const
{
    return eth.local_ipv4(out);
}
