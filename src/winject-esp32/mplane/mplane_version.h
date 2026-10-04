#ifndef WINJECT_MPLANE_MPLANE_VERSION_H_
#define WINJECT_MPLANE_MPLANE_VERSION_H_

#include <stdint.h>

// Parses `vX.Y.Z` only (leading `v` required). X and Y are 0–255; Z is 0–65535.
bool mplane_parse_version(const char* text, uint8_t* x, uint8_t* y, uint16_t* z);

#endif  // WINJECT_MPLANE_MPLANE_VERSION_H_
