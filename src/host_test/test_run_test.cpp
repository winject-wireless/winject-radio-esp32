#include "test_run.h"

#include <gtest/gtest.h>

TEST(TestRunTest, StartWhileRunningIsAlready)
{
    test_run run;
    EXPECT_EQ(run.begin(1), mplane_status::ok);
    EXPECT_TRUE(run.running());
    EXPECT_EQ(run.begin(2), mplane_status::already);
}

TEST(TestRunTest, RepeatedIdIsStaleEvenAfterFinish)
{
    test_run run;
    EXPECT_EQ(run.begin(5), mplane_status::ok);
    run.finish();
    EXPECT_FALSE(run.running());
    EXPECT_EQ(run.begin(5), mplane_status::stale);
    EXPECT_EQ(run.begin(6), mplane_status::ok);
}

TEST(TestRunTest, MissingIdNeverStale)
{
    test_run run;
    EXPECT_EQ(run.begin(std::nullopt), mplane_status::ok);
    run.finish();
    EXPECT_EQ(run.begin(std::nullopt), mplane_status::ok);
}

TEST(TestRunTest, StopChecksRunningId)
{
    test_run run;
    EXPECT_EQ(run.stop(9), mplane_status::ok);  // idle
    EXPECT_FALSE(run.stop_requested());

    ASSERT_EQ(run.begin(3), mplane_status::ok);
    EXPECT_EQ(run.stop(4), mplane_status::stale);
    EXPECT_FALSE(run.stop_requested());
    EXPECT_EQ(run.stop(3), mplane_status::ok);
    EXPECT_TRUE(run.stop_requested());
}

TEST(TestRunTest, StopWithoutIdStopsAnyRun)
{
    test_run run;
    ASSERT_EQ(run.begin(7), mplane_status::ok);
    EXPECT_EQ(run.stop(std::nullopt), mplane_status::ok);
    EXPECT_TRUE(run.stop_requested());
}

TEST(TestRunTest, BeginClearsPreviousStopRequest)
{
    test_run run;
    ASSERT_EQ(run.begin(1), mplane_status::ok);
    ASSERT_EQ(run.stop(1), mplane_status::ok);
    run.finish();
    ASSERT_EQ(run.begin(2), mplane_status::ok);
    EXPECT_FALSE(run.stop_requested());
}

TEST(TestRunTest, AbortBeginKeepsIdUsable)
{
    test_run run;
    ASSERT_EQ(run.begin(1), mplane_status::ok);
    run.finish();
    ASSERT_EQ(run.begin(2), mplane_status::ok);
    run.abort_begin();
    EXPECT_FALSE(run.running());
    EXPECT_EQ(run.begin(2), mplane_status::ok);
    run.finish();
    EXPECT_EQ(run.begin(2), mplane_status::stale);
}

TEST(TestFrameIntervalTest, Pacing)
{
    EXPECT_EQ(test_frame_interval_us(1000, 0), 0u);
    // 1000 B = 8000 bit at 8000 kbps -> 1000 us.
    EXPECT_EQ(test_frame_interval_us(1000, 8000), 1000u);
    // Rounds up so the rate is never exceeded.
    EXPECT_EQ(test_frame_interval_us(1, 3), 2667u);
    EXPECT_EQ(test_frame_interval_us(1472, 1000000), 12u);
}
