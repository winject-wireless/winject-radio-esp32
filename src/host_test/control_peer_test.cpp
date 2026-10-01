#include "control_peer.h"

#include <gtest/gtest.h>

TEST(ControlPeer, OpenBenchAcceptsAny)
{
    EXPECT_TRUE(control_peer_allowed_for(0x01020304u, 0u));
    EXPECT_TRUE(control_peer_allowed_for(0xC0A80164u, 0u));
}

TEST(ControlPeer, TrustedHostOnly)
{
    const uint32_t trusted = 0xC0A8FD01u;  // 192.168.253.1 host order
    uint32_t be_ok = 0;
    uint8_t* b = reinterpret_cast<uint8_t*>(&be_ok);
    b[0] = 192;
    b[1] = 168;
    b[2] = 253;
    b[3] = 1;
    EXPECT_TRUE(control_peer_allowed_for(be_ok, trusted));
    uint32_t be_other = 0;
    b = reinterpret_cast<uint8_t*>(&be_other);
    b[0] = 192;
    b[1] = 168;
    b[2] = 1;
    b[3] = 2;
    EXPECT_FALSE(control_peer_allowed_for(be_other, trusted));
}
