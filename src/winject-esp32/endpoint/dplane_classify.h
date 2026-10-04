#ifndef WINJECT_DPLANE_CLASSIFY_H_
#define WINJECT_DPLANE_CLASSIFY_H_

#include "config.h"

#include <stdint.h>

// Payload length on DPLANE_PORT (decision 8).
enum class dplane_kind
{
    registration,
    mpdu,
    invalid,
};

inline dplane_kind dplane_classify(uint32_t payload_len)
{
    if (payload_len == 0 || payload_len > DPLANE_INJECT_MPDU_MAX)
    {
        return dplane_kind::invalid;
    }
    if (payload_len < static_cast<uint32_t>(WIFI_RADIO_INJECT_MIN))
    {
        return dplane_kind::registration;
    }
    return dplane_kind::mpdu;
}

#endif  // WINJECT_DPLANE_CLASSIFY_H_
