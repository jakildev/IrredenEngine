#include <gtest/gtest.h>
#include <irreden/render/gpu_encoder_intervals.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {
using namespace IRRender;

GpuEncoderIntervalResult resolve(
    const std::vector<std::uint64_t> &samples,
    std::size_t encoders,
    std::size_t capacity,
    std::uint64_t previousBoundary = 0
) {
    std::array<GpuEncoderInterval, 16> scratch{};
    return resolveGpuEncoderIntervals(samples, encoders, capacity, previousBoundary, scratch);
}

TEST(GpuEncoderIntervals, DisjointIntervalsSumTheirLengths) {
    const auto result = resolve({100, 110, 200, 230, 300, 305}, 3, 8);
    ASSERT_TRUE(result.valid_);
    EXPECT_TRUE(result.exclusiveValid_);
    EXPECT_EQ(result.exclusiveTicks_, 10u + 30u + 5u);
    EXPECT_EQ(result.windowTicks_, 205u);
}

TEST(GpuEncoderIntervals, NestedIntervalCountsOnce) {
    const auto result = resolve({100, 200, 120, 150}, 2, 8);
    ASSERT_TRUE(result.valid_);
    EXPECT_EQ(result.exclusiveTicks_, 100u);
    EXPECT_EQ(result.windowTicks_, 100u);
}

TEST(GpuEncoderIntervals, OverlappingIntervalsMergeInAnyOrder) {
    // Encoder order is creation order; the GPU can start a later one first.
    const auto result = resolve({150, 260, 100, 180, 400, 410}, 3, 8);
    ASSERT_TRUE(result.valid_);
    EXPECT_EQ(result.exclusiveTicks_, (260u - 100u) + 10u);
    EXPECT_EQ(result.windowTicks_, 310u);
}

TEST(GpuEncoderIntervals, AStageInsideAnotherStagesWorkReadsItsOwnTime) {
    // One short encoder ahead of a long run of another stage's work and one
    // short encoder after it: the window holds the other stage, the union does not.
    const auto result = resolve({1000, 1010, 9000, 9030}, 2, 8);
    ASSERT_TRUE(result.valid_);
    EXPECT_EQ(result.exclusiveTicks_, 40u);
    EXPECT_EQ(result.windowTicks_, 8030u);
}

TEST(GpuEncoderIntervals, MoreEncodersThanCapacityReadsWindowOnly) {
    // Capacity 2: encoder 0 owns pair 0; encoders 1..3 share pair 1, whose
    // start is encoder 1's and whose end is the last re-write.
    const auto result = resolve({100, 110, 200, 500}, 4, 2);
    ASSERT_TRUE(result.valid_);
    EXPECT_FALSE(result.exclusiveValid_);
    EXPECT_EQ(result.windowTicks_, 400u);
    EXPECT_EQ(result.exclusiveTicks_, 0u);
}

TEST(GpuEncoderIntervals, ExactlyCapacityEncodersStaysExclusive) {
    const auto result = resolve({100, 110, 200, 210}, 2, 2);
    ASSERT_TRUE(result.valid_);
    EXPECT_TRUE(result.exclusiveValid_);
    EXPECT_EQ(result.exclusiveTicks_, 20u);
}

TEST(GpuEncoderIntervals, AHalfWrittenOrReversedPairInvalidates) {
    EXPECT_FALSE(resolve({0, 10, 200, 210}, 2, 8).valid_);
    EXPECT_FALSE(resolve({100, 90, 200, 210}, 2, 8).valid_);
    EXPECT_FALSE(resolve({100, 110, kGpuCounterErrorValue, 210}, 2, 8).valid_);
    EXPECT_FALSE(resolve({100, 110, 200, kGpuCounterErrorValue}, 2, 8).valid_);
    EXPECT_FALSE(resolve({100, 110, 200, 210}, 2, 8, /*previousBoundary=*/105).valid_);
    EXPECT_TRUE(resolve({100, 110, 200, 210}, 2, 8, /*previousBoundary=*/99).valid_);
}

TEST(GpuEncoderIntervals, AnEncoderTheGpuNeverRanIsSkipped) {
    // Pair 1 was never written (zero); pair 2 still holds an earlier frame's samples.
    const auto result = resolve({1000, 1010, 0, 0, 40, 50, 1100, 1130}, 4, 8, 500);
    ASSERT_TRUE(result.valid_);
    EXPECT_TRUE(result.exclusiveValid_);
    EXPECT_EQ(result.exclusiveTicks_, 40u);
    EXPECT_EQ(result.windowTicks_, 130u);
}

TEST(GpuEncoderIntervals, NoExecutedEncoderIsInvalidNotZero) {
    EXPECT_FALSE(resolve({0, 0, 0, 0}, 2, 8).valid_);
    EXPECT_FALSE(resolve({100, 110}, 1, 8, /*previousBoundary=*/150).valid_);
    EXPECT_FALSE(resolve({}, 0, 8).valid_);
    EXPECT_FALSE(resolve({100, 110}, 2, 8).valid_);
}

} // namespace
