#include <irreden/video/ir_video_types.hpp>

#include <gtest/gtest.h>

namespace {

using IRVideo::detail::AutoCapturePacing;

static_assert(
    IRVideo::detail::autoCapturePacingFrom(true, false, false, false, false) ==
    AutoCapturePacing::FIXED_STEP
);
static_assert(
    IRVideo::detail::autoCapturePacingFrom(true, false, true, false, false) ==
    AutoCapturePacing::REAL_TIME_REQUESTED
);
static_assert(
    IRVideo::detail::autoCapturePacingFrom(true, false, false, true, false) ==
    AutoCapturePacing::REAL_TIME_REQUESTED
);
static_assert(
    IRVideo::detail::autoCapturePacingFrom(true, false, false, false, true) ==
    AutoCapturePacing::REAL_TIME_AUDIO_INPUT
);
static_assert(IRVideo::detail::autoRecordWindowStep(0, 1, 1).seenTicks_ == 1);
static_assert(!IRVideo::detail::autoRecordWindowStep(0, 1, 1).stop_);
static_assert(IRVideo::detail::autoRecordWindowStep(1, 2, 1).stop_);

TEST(AutoCapturePacingTest, CoversEveryInputCombination) {
    for (int mask = 0; mask < 32; ++mask) {
        const bool autoRecord = (mask & 1) != 0;
        const bool deterministic = (mask & 2) != 0;
        const bool configRealTime = (mask & 4) != 0;
        const bool commandRealTime = (mask & 8) != 0;
        const bool audioArmed = (mask & 16) != 0;
        const bool requested = configRealTime || commandRealTime;

        AutoCapturePacing expected = AutoCapturePacing::FIXED_STEP;
        if (deterministic && (requested || audioArmed)) {
            expected = AutoCapturePacing::FIXED_STEP_DETERMINISTIC_CAPTURE;
        } else if (autoRecord && requested) {
            expected = AutoCapturePacing::REAL_TIME_REQUESTED;
        } else if (autoRecord && audioArmed) {
            expected = AutoCapturePacing::REAL_TIME_AUDIO_INPUT;
        }

        EXPECT_EQ(
            IRVideo::detail::autoCapturePacingFrom(
                autoRecord,
                deterministic,
                configRealTime,
                commandRealTime,
                audioArmed
            ),
            expected
        ) << "mask="
          << mask;
    }
}

TEST(AutoCapturePacingTest, RequestedReasonWinsOverAudioInput) {
    EXPECT_EQ(
        IRVideo::detail::autoCapturePacingFrom(true, false, false, true, true),
        AutoCapturePacing::REAL_TIME_REQUESTED
    );
}

TEST(AutoRecordWindowTest, OneTickPerPassStopsOnFollowingPass) {
    for (const int target : {0, 1, 600}) {
        std::int64_t seen = 0;
        int stopPass = 0;
        for (int pass = 1; pass <= target + 2; ++pass) {
            const auto step = IRVideo::detail::autoRecordWindowStep(seen, pass, target);
            seen = step.seenTicks_;
            if (step.stop_) {
                stopPass = pass;
                break;
            }
        }
        EXPECT_EQ(stopPass, target + 1) << "target=" << target;
    }
}

TEST(AutoRecordWindowTest, BurstLatchesWholeTickWindowThenStops) {
    auto step = IRVideo::detail::autoRecordWindowStep(599, 602, 600);
    EXPECT_FALSE(step.stop_);
    EXPECT_EQ(step.seenTicks_, 602);
    step = IRVideo::detail::autoRecordWindowStep(step.seenTicks_, 603, 600);
    EXPECT_TRUE(step.stop_);
    EXPECT_EQ(step.seenTicks_, 602);
}

TEST(AutoRecordWindowTest, PassWithoutNewTickDoesNotStopEarly) {
    const auto step = IRVideo::detail::autoRecordWindowStep(42, 42, 600);
    EXPECT_FALSE(step.stop_);
    EXPECT_EQ(step.seenTicks_, 42);
}

} // namespace
