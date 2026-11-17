#include <gtest/gtest.h>

#include <irreden/video/video_recorder.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <vector>

// The encoder queue's two policies against the real encoder. The encoder is
// held while frames are submitted, so the queue is full after exactly
// kMaxQueuedVideoFrames of them on any host.

namespace {

using IRVideo::VideoRecorder;

constexpr int kWidth = 64;
constexpr int kHeight = 64;
constexpr std::size_t kQueueSize = VideoRecorder::kMaxQueuedVideoFrames;

bool startRecorder(VideoRecorder &recorder, const char *fileName, bool blockWhenFull) {
    IRVideo::VideoRecorderConfig config;
    config.output_file_path_ = (std::filesystem::temp_directory_path() / fileName).string();
    config.width_ = kWidth;
    config.height_ = kHeight;
    config.source_width_ = kWidth;
    config.source_height_ = kHeight;
    config.audio_mux_enabled_ = false;
    config.audio_wav_enabled_ = false;
    config.block_when_queue_full_ = blockWhenFull;
    return recorder.start(config);
}

bool submitFrame(VideoRecorder &recorder) {
    const std::vector<std::uint8_t> frame(static_cast<std::size_t>(kWidth) * kHeight * 4, 0x80);
    return recorder.submitVideoFrame(frame.data(), kWidth * 4);
}

TEST(VideoRecorderQueueTest, BlockingQueueWaitsForTheEncoderAndKeepsEveryFrame) {
    VideoRecorder recorder;
    if (!startRecorder(recorder, "ir_recorder_queue_blocking.mp4", true)) {
        GTEST_SKIP() << "no encoder in this build: " << recorder.getLastError();
    }
    recorder.holdEncoderForTesting(true);
    for (std::size_t i = 0; i < kQueueSize; ++i) {
        ASSERT_TRUE(submitFrame(recorder)) << recorder.getLastError();
    }

    // A correct wait never returns while the encoder is held, so the timeout
    // only bounds how long a dropping regression takes to show.
    std::future<bool> overflow =
        std::async(std::launch::async, [&recorder]() { return submitFrame(recorder); });
    EXPECT_EQ(overflow.wait_for(std::chrono::milliseconds(200)), std::future_status::timeout);

    recorder.holdEncoderForTesting(false);
    EXPECT_TRUE(overflow.get()) << recorder.getLastError();
    recorder.stop();
    EXPECT_EQ(recorder.getVideoFrameCount(), kQueueSize + 1);
    EXPECT_EQ(recorder.getDroppedVideoFrameCount(), 0u);
}

TEST(VideoRecorderQueueTest, DroppingQueueAcceptsEveryFrameAndCountsItsLosses) {
    VideoRecorder recorder;
    if (!startRecorder(recorder, "ir_recorder_queue_dropping.mp4", false)) {
        GTEST_SKIP() << "no encoder in this build: " << recorder.getLastError();
    }
    constexpr std::size_t kOverflow = 5;
    recorder.holdEncoderForTesting(true);
    for (std::size_t i = 0; i < kQueueSize + kOverflow; ++i) {
        ASSERT_TRUE(submitFrame(recorder)) << recorder.getLastError();
    }
    EXPECT_EQ(recorder.getDroppedVideoFrameCount(), kOverflow);
    recorder.stop();
    EXPECT_EQ(recorder.getVideoFrameCount(), kQueueSize + kOverflow);
    EXPECT_EQ(recorder.getDroppedVideoFrameCount(), kOverflow);
}

} // namespace
