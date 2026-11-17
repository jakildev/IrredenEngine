#include <gtest/gtest.h>

#include <irreden/video/video_recorder.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

// The encoder queue's two policies against the real encoder. Frames far
// larger than it keeps up with are submitted with no pause between them, so
// the queue fills within the first few.

namespace {

constexpr int kWidth = 3840;
constexpr int kHeight = 2160;
constexpr int kFrames = 40;

bool startRecorder(IRVideo::VideoRecorder &recorder, const char *fileName, bool blockWhenFull) {
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

// Two alternating frames of incompressible texels, so no frame is a cheap
// copy of the one before it.
void submitFrames(IRVideo::VideoRecorder &recorder) {
    const std::size_t bytes = static_cast<std::size_t>(kWidth) * kHeight * 4;
    std::vector<std::uint8_t> frames[2] = {
        std::vector<std::uint8_t>(bytes),
        std::vector<std::uint8_t>(bytes)
    };
    std::uint32_t word = 0x9E3779B9u;
    for (std::vector<std::uint8_t> &frame : frames) {
        for (std::uint8_t &texel : frame) {
            word = word * 1664525u + 1013904223u;
            texel = static_cast<std::uint8_t>(word >> 24);
        }
    }
    for (int i = 0; i < kFrames; ++i) {
        ASSERT_TRUE(recorder.submitVideoFrame(frames[i % 2].data(), kWidth * 4))
            << recorder.getLastError();
    }
}

TEST(VideoRecorderQueueTest, BlockingQueueKeepsEveryFrame) {
    IRVideo::VideoRecorder recorder;
    if (!startRecorder(recorder, "ir_recorder_queue_blocking.mp4", true)) {
        GTEST_SKIP() << "no encoder in this build: " << recorder.getLastError();
    }
    submitFrames(recorder);
    recorder.stop();
    EXPECT_EQ(recorder.getVideoFrameCount(), static_cast<std::uint64_t>(kFrames));
    EXPECT_EQ(recorder.getDroppedVideoFrameCount(), 0u);
}

// Whether the encoder falls behind is the host's speed, so the count is only
// bounded here; every frame is still accepted.
TEST(VideoRecorderQueueTest, DroppingQueueAcceptsEveryFrameAndCountsItsLosses) {
    IRVideo::VideoRecorder recorder;
    if (!startRecorder(recorder, "ir_recorder_queue_dropping.mp4", false)) {
        GTEST_SKIP() << "no encoder in this build: " << recorder.getLastError();
    }
    submitFrames(recorder);
    recorder.stop();
    EXPECT_EQ(recorder.getVideoFrameCount(), static_cast<std::uint64_t>(kFrames));
    EXPECT_LT(recorder.getDroppedVideoFrameCount(), static_cast<std::uint64_t>(kFrames));
    RecordProperty("dropped_frames", static_cast<int>(recorder.getDroppedVideoFrameCount()));
}

} // namespace
