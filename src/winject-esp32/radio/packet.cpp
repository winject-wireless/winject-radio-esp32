#include "packet.h"

#include <cstddef>
#include <cstdlib>
#include <utility>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char* TAG = "packet";

packet::packet(packet_allocator& alloc, uint8_t* buf, size_t capacity)
    : alloc(&alloc), buf(buf), capacity_(capacity)
{
}

packet::packet(packet&& other) noexcept
{
    steal_from(other);
}

packet& packet::operator=(packet&& other) noexcept
{
    if (this != &other)
    {
        reset();
        steal_from(other);
    }
    return *this;
}

packet::~packet()
{
    reset();
}

void packet::steal_from(packet& other) noexcept
{
    alloc = other.alloc;
    buf = other.buf;
    heap_owner_ = other.heap_owner_;
    capacity_ = other.capacity_;
    size_ = other.size_;
    other.clear();
}

void packet::clear() noexcept
{
    alloc = nullptr;
    buf = nullptr;
    heap_owner_ = nullptr;
    capacity_ = 0;
    size_ = 0;
}

void packet::set_packet_size(size_t size)
{
    size_ = size;
}

void packet::reset()
{
    if (heap_owner_ != nullptr)
    {
        std::free(heap_owner_);
    }
    else if (alloc != nullptr && buf != nullptr)
    {
        alloc->release(buf);
    }
    clear();
}

packet packet::adopt_heap(uint8_t* heap_owner, const uint8_t* payload,
                          size_t len)
{
    packet p;
    if (heap_owner == nullptr || payload == nullptr || len == 0)
    {
        return p;
    }
    p.heap_owner_ = heap_owner;
    p.buf = const_cast<uint8_t*>(payload);
    p.capacity_ = len;
    p.size_ = len;
    return p;
}

bool packet::is_valid() const
{
    return buf != nullptr && size_ <= capacity_;
}

uint8_t* packet::data()
{
    return buf;
}

const uint8_t* packet::data() const
{
    return buf;
}

size_t packet::size() const
{
    return size_;
}

size_t packet::capacity() const
{
    return buf == nullptr ? 0 : capacity_;
}

void packet_allocator::free_deleter::operator()(uint8_t* p) const
{
    std::free(p);
}

packet_allocator& packet_allocator::rx()
{
    static packet_allocator inst;
    return inst;
}

bool packet_allocator::init(size_t count)
{
    if (free != nullptr)
    {
        return true;
    }
    if (count == 0 || count > k_max_count)
    {
        ESP_LOGE(TAG, "rx pool count %u out of range 1..%u",
                 static_cast<unsigned>(count),
                 static_cast<unsigned>(k_max_count));
        return false;
    }
    std::unique_ptr<uint8_t, free_deleter> mem(
        static_cast<uint8_t*>(std::malloc(count * k_buf_size)));
    if (!mem)
    {
        ESP_LOGE(TAG, "rx pool alloc %u x %u failed",
                 static_cast<unsigned>(count),
                 static_cast<unsigned>(k_buf_size));
        return false;
    }
    QueueHandle_t q =
        xQueueCreate(static_cast<UBaseType_t>(count), sizeof(uint8_t));
    if (q == nullptr)
    {
        ESP_LOGE(TAG, "rx pool free-list alloc failed");
        return false;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        if (xQueueSend(q, &i, 0) != pdTRUE)
        {
            ESP_LOGE(TAG, "rx pool free-list seed failed at %u",
                     static_cast<unsigned>(i));
            vQueueDelete(q);
            return false;
        }
    }
    storage = std::move(mem);
    this->count = count;
    free = q;
    return true;
}

packet packet_allocator::allocate()
{
    packet out;
    if (free == nullptr)
    {
        return out;
    }
    uint8_t idx = 0;
    if (xQueueReceive(static_cast<QueueHandle_t>(free), &idx, 0) != pdTRUE ||
        idx >= count)
    {
        return out;
    }
    return packet(*this, storage.get() + static_cast<size_t>(idx) * k_buf_size,
                  k_buf_size);
}

size_t packet_allocator::available() const
{
    if (free == nullptr)
    {
        return 0;
    }
    return uxQueueMessagesWaiting(static_cast<QueueHandle_t>(free));
}

void packet_allocator::release(uint8_t* buf)
{
    uint8_t idx = 0;
    if (!index_of(buf, &idx) || free == nullptr)
    {
        return;
    }
    xQueueSend(static_cast<QueueHandle_t>(free), &idx, 0);
}

bool packet_allocator::index_of(const uint8_t* buf, uint8_t* idx) const
{
    if (buf == nullptr || idx == nullptr || count == 0)
    {
        return false;
    }
    const ptrdiff_t off = buf - storage.get();
    if (off < 0 || (off % static_cast<ptrdiff_t>(k_buf_size)) != 0)
    {
        return false;
    }
    const ptrdiff_t i = off / static_cast<ptrdiff_t>(k_buf_size);
    if (i >= static_cast<ptrdiff_t>(count))
    {
        return false;
    }
    *idx = static_cast<uint8_t>(i);
    return true;
}
