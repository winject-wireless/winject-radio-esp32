#ifndef WINJECT_RADIO_FCS_H_
#define WINJECT_RADIO_FCS_H_

#include <stddef.h>
#include <stdint.h>

// 802.11 FCS: CRC-32 (IEEE 802.3 polynomial) over the MPDU, sent LSB first.
uint32_t wifi_fcs_compute(const uint8_t* mpdu, size_t len);
// frame = MPDU followed by its 4-byte FCS; len includes the FCS.
bool wifi_fcs_matches(const uint8_t* frame, size_t len);
// Writes the FCS of mpdu[0..len) to out[0..4).
void wifi_fcs_store(const uint8_t* mpdu, size_t len, uint8_t out[4]);

#endif  // WINJECT_RADIO_FCS_H_
