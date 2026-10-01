#ifndef WINJECT_PACKET_H_
#define WINJECT_PACKET_H_

#include "config.h"

#include <stddef.h>
#include <stdint.h>
#include <atomic>
#include <memory>

class packet_allocator;

// Move-only owner of one frame buffer: either an RX pool slot or a malloc'd
// block (adopt_heap). The buffer is returned / freed on reset or destruction.
class packet
{
public:
    packet() = default;
    packet(packet&& other) noexcept;
    packet& operator=(packet&& other) noexcept;
    ~packet();

    packet(const packet&) = delete;
    packet& operator=(const packet&) = delete;

    void set_packet_size(size_t size);
    void reset();

    bool is_valid() const;
    uint8_t* data();
    const uint8_t* data() const;
    size_t size() const;
    size_t capacity() const;

    // Takes ownership of a malloc'd heap_owner (freed on reset); payload points
    // inside it. Used for EMAC input frames (no copy) and test frames.
    static packet adopt_heap(uint8_t* heap_owner, const uint8_t* payload,
                             size_t len);

private:
    friend class packet_allocator;
    packet(packet_allocator& alloc, uint8_t* buf, size_t capacity);

    void steal_from(packet& other) noexcept;
    void clear() noexcept;

    packet_allocator* alloc = nullptr;
    uint8_t* buf = nullptr;
    uint8_t* heap_owner_ = nullptr;
    size_t capacity_ = 0;
    size_t size_ = 0;
};

// Fixed pool of WIFI_RX_PACKET_CAP buffers for air→UDP frames. allocate() runs
// in the WiFi promiscuous callback; release from the d-plane drain task.
class packet_allocator
{
public:
    static packet_allocator& rx();

    static constexpr size_t k_buf_size = WIFI_RX_PACKET_CAP;
    static constexpr size_t k_max_count = WIFI_RX_QUEUE_MAX;

    // Heap-allocates count × k_buf_size once (boot); count is tune rx_queue_sz.
    bool init(size_t count);
    packet allocate();
    size_t available() const;

private:
    friend class packet;

    struct free_deleter
    {
        void operator()(uint8_t* p) const;
    };

    packet_allocator() = default;
    packet_allocator(const packet_allocator&) = delete;
    packet_allocator& operator=(const packet_allocator&) = delete;

    void release(uint8_t* buf);
    bool index_of(const uint8_t* buf, uint8_t* idx) const;

    std::unique_ptr<uint8_t, free_deleter> storage;
    size_t count = 0;
    void* free = nullptr;  // QueueHandle_t of free slot indices
};

#endif  // WINJECT_PACKET_H_
