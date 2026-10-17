#include <irreden/time/time_manager.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace {

using IRTime::TimeManager;

constexpr auto kSuspension = std::chrono::milliseconds(300);

int dueUpdateTicks(TimeManager &time) {
    time.beginMainLoop();
    time.clampUpdateLag(8);
    int ticks = 0;
    while (time.shouldUpdate() && ticks < 100) {
        time.beginEvent<IRTime::UPDATE>();
        time.endEvent<IRTime::UPDATE>();
        ++ticks;
    }
    return ticks;
}

// The control arms show the suspension would otherwise reach the clock: a
// full catch-up burst and a RENDER delta as long as the handoff.
TEST(TimeManagerHandOffTest, SuspensionWithoutResumeBurstsCatchUpTicks) {
    TimeManager time;
    time.start();
    std::this_thread::sleep_for(kSuspension);
    EXPECT_EQ(dueUpdateTicks(time), 8);
}

TEST(TimeManagerHandOffTest, ResumeDropsTheSuspensionFromUpdateLag) {
    TimeManager time;
    time.start();
    std::this_thread::sleep_for(kSuspension);
    time.resumeAfterSuspend();
    EXPECT_LE(dueUpdateTicks(time), 2);
}

TEST(TimeManagerHandOffTest, SuspensionWithoutResumeStretchesTheRenderDelta) {
    TimeManager time;
    time.start();
    std::this_thread::sleep_for(kSuspension);
    time.beginEvent<IRTime::RENDER>();
    EXPECT_GE(time.deltaTime<IRTime::RENDER>(), 0.3);
}

TEST(TimeManagerHandOffTest, ResumeDropsTheSuspensionFromTheRenderDelta) {
    TimeManager time;
    time.start();
    std::this_thread::sleep_for(kSuspension);
    time.resumeAfterSuspend();
    time.beginEvent<IRTime::RENDER>();
    EXPECT_LT(time.deltaTime<IRTime::RENDER>(), 0.15);
}

} // namespace
