#ifndef WINJECT_CONTROL_PEER_H_
#define WINJECT_CONTROL_PEER_H_

#include "config.h"

#include <stdint.h>

// Trusted control-plane peers (m-plane, d-plane, HTTP OTA). No ESP-IDF deps.

static inline uint32_t control_ipv4_host_from_be(uint32_t src_be)
{
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&src_be);
    return (static_cast<uint32_t>(b[0]) << 24) |
           (static_cast<uint32_t>(b[1]) << 16) |
           (static_cast<uint32_t>(b[2]) << 8) |
           static_cast<uint32_t>(b[3]);
}

static inline bool control_peer_allowed_for(uint32_t src_be,
                                            uint32_t trusted_host)
{
    if (trusted_host == 0u)
    {
        return true;
    }
    return control_ipv4_host_from_be(src_be) == trusted_host;
}

static inline bool control_peer_allowed(uint32_t src_be)
{
    return control_peer_allowed_for(src_be, CONTROL_TRUSTED_IPV4);
}

#endif  // WINJECT_CONTROL_PEER_H_
