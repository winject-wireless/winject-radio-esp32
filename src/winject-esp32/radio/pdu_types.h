#ifndef WINJECT_PDU_TYPES_H_
#define WINJECT_PDU_TYPES_H_

#include <stdint.h>

// host = IPv4, network byte order
struct ip_port_t
{
    uint32_t host;
    uint16_t port;
};

inline bool ip_port_eq(const ip_port_t& a, const ip_port_t& b)
{
    return a.host == b.host && a.port == b.port;
}

#endif  // WINJECT_PDU_TYPES_H_
