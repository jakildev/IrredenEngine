// IRRender::OffscreenFramePacer is the deadline rule of the Metal OFFSCREEN
// pacer. Synthetic time points stand in for the clock: each case says when a
// frame completed and checks the time the pacer asks it to sleep until.

#include <gtest/gtest.h>

#include <irreden/render/offscreen_frame_pacer.hpp>

#include <chrono>

namespace {

using IRRender::OffscreenFramePacer;
using Clock = OffscreenFramePacer::Clock;
using std::chrono::microseconds;

// 120 Hz, rounded to a whole microsecond so sums stay exact.
constexpr Clock::duration kInterval =
    std::chrono::duration_cast<Clock::duration>(microseconds{8333});

Clock::time_point origin() {
    return Clock::time_point{} + std::chrono::hours{1};
}

TEST(OffscreenFramePacer, LateFrameRequestsNoSleepAndReanchorsAtCompletion) {
    OffscreenFramePacer pacer;
    pacer.reset(kInterval, origin());

    const Clock::time_point late = origin() + kInterval + microseconds{3000};
    EXPECT_EQ(pacer.frameCompleted(late), late);
    EXPECT_EQ(pacer.lastDeadline(), late);

    // The next frame is measured from the late frame's completion: an early
    // finish sleeps to one interval past it, not to the old schedule.
    const Clock::time_point early = late + microseconds{1000};
    EXPECT_EQ(pacer.frameCompleted(early), late + kInterval);
}

TEST(OffscreenFramePacer, FrameLateByMoreThanOneIntervalRequestsNoSleep) {
    OffscreenFramePacer pacer;
    pacer.reset(kInterval, origin());

    const Clock::time_point veryLate = origin() + 3 * kInterval + microseconds{500};
    EXPECT_EQ(pacer.frameCompleted(veryLate), veryLate);
}

TEST(OffscreenFramePacer, FrameCompletingExactlyAtItsDeadlineRequestsNoSleep) {
    OffscreenFramePacer pacer;
    pacer.reset(kInterval, origin());

    const Clock::time_point onDeadline = origin() + kInterval;
    EXPECT_EQ(pacer.frameCompleted(onDeadline), onDeadline);
}

TEST(OffscreenFramePacer, EarlyFrameSleepsToOneIntervalPastThePreviousDeadline) {
    OffscreenFramePacer pacer;
    pacer.reset(kInterval, origin());

    const Clock::time_point early = origin() + microseconds{2000};
    EXPECT_EQ(pacer.frameCompleted(early), origin() + kInterval);
    EXPECT_EQ(pacer.lastDeadline(), origin() + kInterval);
}

TEST(OffscreenFramePacer, RunOfEarlyFramesAccumulatesNoDrift) {
    OffscreenFramePacer pacer;
    pacer.reset(kInterval, origin());

    // Each frame completes 2 ms after the previous deadline, i.e. well
    // before its own; the schedule must stay on the interval lattice.
    Clock::time_point deadline = origin();
    for (int frame = 0; frame < 1000; ++frame) {
        deadline = pacer.frameCompleted(deadline + microseconds{2000});
    }
    EXPECT_EQ(deadline, origin() + 1000 * kInterval);
}

TEST(OffscreenFramePacer, ResetRestartsTheScheduleFromTheGivenStart) {
    OffscreenFramePacer pacer;
    pacer.reset(kInterval, origin());
    pacer.frameCompleted(origin() + microseconds{100});

    const Clock::time_point restart = origin() + std::chrono::seconds{5};
    const Clock::duration interval60 =
        std::chrono::duration_cast<Clock::duration>(microseconds{16667});
    pacer.reset(interval60, restart);
    EXPECT_EQ(pacer.frameCompleted(restart + microseconds{100}), restart + interval60);
}

} // namespace
