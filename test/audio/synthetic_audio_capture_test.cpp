#include <irreden/audio/synthetic_capture_source.hpp>
#include <irreden/ir_math.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace {

TEST(SyntheticAudioCaptureTest, DeliversWallClockPacedMonotonicAudioAndStopsSynchronously) {
    constexpr int kSampleRate = 48'000;
    constexpr int kBufferFrames = 1024;
    IRAudio::SyntheticAudioCaptureSource source;
    std::atomic<int> deliveredFrames = 0;
    std::atomic<int> callbackCount = 0;
    std::atomic<bool> overflowSeen = false;
    std::mutex timesMutex;
    std::vector<double> streamTimes;

    const auto startedAt = std::chrono::steady_clock::now();
    ASSERT_TRUE(source.startCapture(
        IRAudio::AudioCaptureConfig{"", kSampleRate, 2},
        [&](const float *, int frames, double streamTime, bool overflow) {
            if (overflow) {
                overflowSeen = true;
            }
            deliveredFrames += frames;
            ++callbackCount;
            std::lock_guard<std::mutex> lock(timesMutex);
            streamTimes.push_back(streamTime);
        }
    ));
    EXPECT_TRUE(source.isCapturing());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    source.stopCapture();
    const auto stoppedAt = std::chrono::steady_clock::now();
    EXPECT_FALSE(source.isCapturing());

    const double elapsedSeconds = std::chrono::duration<double>(stoppedAt - startedAt).count();
    const int expectedFrames = static_cast<int>(elapsedSeconds * kSampleRate);
    EXPECT_LE(IRMath::abs(deliveredFrames.load() - expectedFrames), kBufferFrames);
    EXPECT_GT(callbackCount.load(), 0);
    EXPECT_FALSE(overflowSeen);
    {
        std::lock_guard<std::mutex> lock(timesMutex);
        for (std::size_t i = 1; i < streamTimes.size(); ++i) {
            EXPECT_GT(streamTimes[i], streamTimes[i - 1]);
        }
    }

    const int callbacksAfterStop = callbackCount.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(callbackCount.load(), callbacksAfterStop);
    EXPECT_DOUBLE_EQ(source.getInputLatencyMs(), 0.0);
}

} // namespace
