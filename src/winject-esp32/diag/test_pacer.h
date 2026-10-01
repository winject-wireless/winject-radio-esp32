#ifndef WINJECT_DIAG_TEST_PACER_H_
#define WINJECT_DIAG_TEST_PACER_H_

#include <stdint.h>

#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Spaces frames interval_us apart (0 = no pacing). Sleeps whole ticks and
// busy-waits the remainder; after a stall it restarts from now instead of
// bursting to catch up.
class test_pacer
{
public:
    explicit test_pacer(uint32_t interval_us)
        : interval_us_(interval_us), next_us_(esp_timer_get_time())
    {
    }

    void wait()
    {
        if (interval_us_ == 0)
        {
            return;
        }
        constexpr int64_t k_tick_us = 1000 * portTICK_PERIOD_MS;
        int64_t now = esp_timer_get_time();
        while (next_us_ > now)
        {
            const int64_t ahead = next_us_ - now;
            if (ahead >= k_tick_us)
            {
                vTaskDelay(static_cast<TickType_t>(ahead / k_tick_us));
            }
            else
            {
                esp_rom_delay_us(static_cast<uint32_t>(ahead));
            }
            now = esp_timer_get_time();
        }
        next_us_ += interval_us_;
        if (next_us_ < now)
        {
            next_us_ = now;
        }
    }

private:
    uint32_t interval_us_;
    int64_t next_us_;
};

#endif  // WINJECT_DIAG_TEST_PACER_H_
