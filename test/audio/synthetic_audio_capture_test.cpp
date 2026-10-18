#include <irreden/audio/synthetic_capture_source.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace {

TEST(SyntheticAudioCaptureTest, ReportsConfiguredFormatAndLevelsOnlyWhileCapturing) {
    IRAudio::SyntheticAudioCaptureSource source;
    EXPECT_EQ(source.getCaptureSampleRate(), 0);
    EXPECT_EQ(source.getCaptureChannels(), 0);
    EXPECT_FALSE(source.getInputLevels().has_value());

    ASSERT_TRUE(source.startCapture(
        IRAudio::AudioCaptureConfig{"", 44'100, 2},
        [](const float *, int, double, bool) {}
    ));
    EXPECT_EQ(source.getCaptureSampleRate(), 44'100);
    EXPECT_EQ(source.getCaptureChannels(), 2);
    ASSERT_TRUE(source.getInputLevels().has_value());
    EXPECT_EQ(source.getInputLevels()->channelCount_, 2u);
    EXPECT_EQ(source.getInputLevels()->sampleRate_, 44'100u);

    source.stopCapture();
    EXPECT_EQ(source.getCaptureSampleRate(), 0);
    EXPECT_EQ(source.getCaptureChannels(), 0);
    EXPECT_FALSE(source.getInputLevels().has_value());
}

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
    // The tone is 0.1 full scale on every channel.
    const std::optional<IRAudio::AudioInputLevels> levels = source.getInputLevels();
    ASSERT_TRUE(levels.has_value());
    EXPECT_GE(levels->generation_, 1u);
    EXPECT_NEAR(levels->channels_[0].peakDbfs_, -20.0f, 0.5f);
    EXPECT_NEAR(levels->channels_[0].rmsDbfs_, -23.01f, 0.5f);
    EXPECT_NEAR(levels->channels_[1].peakDbfs_, -20.0f, 0.5f);
    const auto stopRequestedAt = std::chrono::steady_clock::now();
    source.stopCapture();
    const auto stoppedAt = std::chrono::steady_clock::now();
    EXPECT_FALSE(source.isCapturing());

    const auto framesDueBy = [&](const auto time) {
        const double elapsedSeconds = std::chrono::duration<double>(time - startedAt).count();
        return static_cast<int>(elapsedSeconds * kSampleRate);
    };
    EXPECT_LE(deliveredFrames.load(), framesDueBy(stoppedAt));
    EXPECT_GE(deliveredFrames.load(), framesDueBy(stopRequestedAt) - 3 * kBufferFrames);
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
