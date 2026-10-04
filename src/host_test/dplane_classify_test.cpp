#include "dplane_classify.h"

#include <gtest/gtest.h>

TEST(DplaneClassifyTest, PayloadLengths)
{
    EXPECT_EQ(dplane_classify(0), dplane_kind::invalid);
    EXPECT_EQ(dplane_classify(1), dplane_kind::registration);
    EXPECT_EQ(dplane_classify(23), dplane_kind::registration);
    EXPECT_EQ(dplane_classify(24), dplane_kind::mpdu);
    EXPECT_EQ(dplane_classify(1472), dplane_kind::mpdu);
    EXPECT_EQ(dplane_classify(1473), dplane_kind::invalid);
}
