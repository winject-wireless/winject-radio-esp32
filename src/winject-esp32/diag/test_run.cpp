#include "test_run.h"

mplane_status test_run::begin(std::optional<uint8_t> id)
{
    if (running_.load(std::memory_order_acquire))
    {
        return mplane_status::already;
    }
    if (id.has_value() && last_id_ == id)
    {
        return mplane_status::stale;
    }
    prev_last_id_ = last_id_;
    last_id_ = id;
    run_id_ = id;
    stop_.store(false, std::memory_order_relaxed);
    running_.store(true, std::memory_order_release);
    return mplane_status::ok;
}

void test_run::abort_begin()
{
    last_id_ = prev_last_id_;
    running_.store(false, std::memory_order_release);
}

mplane_status test_run::stop(std::optional<uint8_t> id)
{
    if (!running_.load(std::memory_order_acquire))
    {
        return mplane_status::ok;
    }
    if (id.has_value() && run_id_.has_value() && *id != *run_id_)
    {
        return mplane_status::stale;
    }
    stop_.store(true, std::memory_order_release);
    return mplane_status::ok;
}

bool test_run::running() const
{
    return running_.load(std::memory_order_acquire);
}

bool test_run::stop_requested() const
{
    return stop_.load(std::memory_order_acquire);
}

void test_run::finish()
{
    running_.store(false, std::memory_order_release);
}

uint32_t test_frame_interval_us(size_t bytes, uint32_t rate_kbps)
{
    if (rate_kbps == 0)
    {
        return 0;
    }
    const uint64_t bits = static_cast<uint64_t>(bytes) * 8u;
    const uint64_t us = (bits * 1000u + rate_kbps - 1) / rate_kbps;
    return us > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(us);
}
