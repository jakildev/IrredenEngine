// IRRender::detail::GpuSubmissionFences is the OpenGL device's submission
// lifecycle: present() fences each frame, then blocks on the oldest fence
// until at most kMaxPresentedSubmissionsInFlight frames are unfinished. A fake
// fence stands in for the GL timestamp query: the test decides which
// submissions the "GPU" has finished and records every blocking wait.

#include <gtest/gtest.h>

#include <irreden/render/gpu_submission_fences.hpp>

#include <set>
#include <vector>

namespace {

using IRRender::detail::GpuSubmissionFences;
using IRRender::detail::kMaxPresentedSubmissionsInFlight;

struct FakeGpu {
    std::set<int> finished_;
    std::vector<int> waits_;
    int nextFence_ = 100;

    auto available() {
        return [this](int fence) { return finished_.count(fence) != 0; };
    }

    // A blocking wait returns once the fence's submission has finished; the
    // fake records it and leaves finished_ alone, so a helper that re-polled
    // the waited fence instead of retiring it would never make progress.
    auto wait() {
        return [this](int fence) { waits_.push_back(fence); };
    }

    // GL present(): fence the frame, then throttle.
    int present(GpuSubmissionFences<int> &fences) {
        fences.pollCompleted(available());
        const int fence = fences.takeRetiredFence().value_or(nextFence_++);
        // Re-issuing a query makes it unavailable until the new frame finishes.
        finished_.erase(fence);
        fences.endSubmission(fence);
        fences.throttle(kMaxPresentedSubmissionsInFlight, available(), wait());
        return fence;
    }
};

TEST(GpuSubmissionFences, TheCpuRunsTwoPresentedFramesAhead) {
    // Pinned: raising it re-opens the multi-frame driver stall, lowering it
    // starves the GPU between frames.
    EXPECT_EQ(kMaxPresentedSubmissionsInFlight, 2u);
}

TEST(GpuSubmissionFences, ThirdUnfinishedPresentBlocksOnTheOldestFrame) {
    GpuSubmissionFences<int> fences;
    FakeGpu gpu;
    EXPECT_EQ(fences.submissionSerial(), 1u);
    EXPECT_EQ(fences.completedSerial(), 0u);

    const int first = gpu.present(fences);
    EXPECT_TRUE(gpu.waits_.empty());
    EXPECT_EQ(fences.inFlight(), 1u);

    gpu.present(fences);
    EXPECT_TRUE(gpu.waits_.empty());
    EXPECT_EQ(fences.inFlight(), 2u);
    EXPECT_EQ(fences.completedSerial(), 0u);

    gpu.present(fences);
    ASSERT_EQ(gpu.waits_.size(), 1u);
    EXPECT_EQ(gpu.waits_[0], first);
    EXPECT_EQ(fences.inFlight(), 2u);
    EXPECT_EQ(fences.completedSerial(), 1u);
    EXPECT_EQ(fences.submissionSerial(), 4u);
}

TEST(GpuSubmissionFences, AWaitedFenceRetiresAndIsReused) {
    GpuSubmissionFences<int> fences;
    FakeGpu gpu;
    const int first = gpu.present(fences);
    gpu.present(fences);
    gpu.present(fences);
    ASSERT_EQ(gpu.waits_.size(), 1u);

    // The waited query is free again: the fourth frame fences with it.
    EXPECT_EQ(gpu.present(fences), first);
    EXPECT_EQ(gpu.waits_.size(), 2u);
    EXPECT_EQ(fences.completedSerial(), 2u);
}

TEST(GpuSubmissionFences, AFinishedOldestFrameNeedsNoWait) {
    GpuSubmissionFences<int> fences;
    FakeGpu gpu;
    gpu.finished_.insert(gpu.present(fences));
    gpu.present(fences);
    gpu.present(fences);

    EXPECT_TRUE(gpu.waits_.empty());
    EXPECT_EQ(fences.completedSerial(), 1u);
    EXPECT_EQ(fences.inFlight(), 2u);
}

TEST(GpuSubmissionFences, AStalledGpuHoldsTheCpuAtTheBoundEveryFrame) {
    GpuSubmissionFences<int> fences;
    FakeGpu gpu;
    for (int frame = 0; frame < 10; ++frame) {
        gpu.present(fences);
        EXPECT_LE(fences.inFlight(), kMaxPresentedSubmissionsInFlight);
    }
    EXPECT_EQ(gpu.waits_.size(), 8u);
    EXPECT_EQ(fences.completedSerial(), 8u);
}

TEST(GpuSubmissionFences, CompletionRetiresInSubmissionOrder) {
    GpuSubmissionFences<int> fences;
    fences.endSubmission(1);
    fences.endSubmission(2);
    fences.endSubmission(3);
    const std::set<int> finished{2, 3};
    const auto available = [&finished](int fence) { return finished.count(fence) != 0; };

    // Serial 2 has finished but serial 1 has not: nothing is complete yet.
    EXPECT_EQ(fences.pollCompleted(available), 0u);
    EXPECT_EQ(fences.inFlight(), 3u);
}

TEST(GpuSubmissionFences, CompleteAllFinishesTheRecordingSubmissionToo) {
    GpuSubmissionFences<int> fences;
    fences.endSubmission(1);
    fences.endSubmission(2);
    ASSERT_EQ(fences.submissionSerial(), 3u);

    fences.completeAll();
    EXPECT_EQ(fences.completedSerial(), 3u);
    EXPECT_EQ(fences.submissionSerial(), 4u);
    EXPECT_EQ(fences.inFlight(), 0u);
    EXPECT_TRUE(fences.takeRetiredFence().has_value());
    EXPECT_TRUE(fences.takeRetiredFence().has_value());
    EXPECT_FALSE(fences.takeRetiredFence().has_value());
}

} // namespace
