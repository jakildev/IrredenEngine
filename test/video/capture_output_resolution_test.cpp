#include <gtest/gtest.h>

#include <irreden/video/ir_video_types.hpp>

// Coverage for `deriveCaptureOutputResolution` — the pure aspect-derivation
// step behind `VideoManager::configureCaptureOutputResolution` /
// `toggleCapture`. Pure by construction (no render manager, no GPU), so the
// interesting cases are pinned at compile time too.

namespace {

using IRVideo::CaptureOutputResolution;
using IRVideo::deriveCaptureOutputResolution;

constexpr CaptureOutputResolution kBothZero = deriveCaptureOutputResolution(0, 0, 1920, 1080);
static_assert(kBothZero.width_ == 1920 && kBothZero.height_ == 1080);

constexpr CaptureOutputResolution kBothNonZero =
    deriveCaptureOutputResolution(640, 480, 1920, 1080);
static_assert(kBothNonZero.width_ == 640 && kBothNonZero.height_ == 480);

// 16:9 render, width-only override: height derives exactly, already even.
constexpr CaptureOutputResolution kWidthOnlyDerived =
    deriveCaptureOutputResolution(640, 0, 1920, 1080);
static_assert(kWidthOnlyDerived.width_ == 640 && kWidthOnlyDerived.height_ == 360);

// Non-16:9 render (4:3), width-only override: height derives from that aspect.
constexpr CaptureOutputResolution kWidthOnly4x3 = deriveCaptureOutputResolution(640, 0, 800, 600);
static_assert(kWidthOnly4x3.width_ == 640 && kWidthOnly4x3.height_ == 480);

// Height-only override derives width from the render aspect.
constexpr CaptureOutputResolution kHeightOnly = deriveCaptureOutputResolution(0, 360, 1920, 1080);
static_assert(kHeightOnly.width_ == 640 && kHeightOnly.height_ == 360);

// An odd derived value rounds down to even.
constexpr CaptureOutputResolution kOddDerivedHeight =
    deriveCaptureOutputResolution(100, 0, 1000, 333);
static_assert(kOddDerivedHeight.width_ == 100 && kOddDerivedHeight.height_ == 32);

TEST(CaptureOutputResolutionTest, BothZeroFollowsRenderOutput) {
    const CaptureOutputResolution resolved = deriveCaptureOutputResolution(0, 0, 1920, 1080);
    EXPECT_EQ(resolved.width_, 1920);
    EXPECT_EQ(resolved.height_, 1080);
}

TEST(CaptureOutputResolutionTest, BothNonZeroKeepsCallerDimensions) {
    const CaptureOutputResolution resolved = deriveCaptureOutputResolution(640, 480, 1920, 1080);
    EXPECT_EQ(resolved.width_, 640);
    EXPECT_EQ(resolved.height_, 480);
}

TEST(CaptureOutputResolutionTest, WidthOnlyDerivesHeightFromRenderAspect) {
    // 16:9 render.
    CaptureOutputResolution resolved = deriveCaptureOutputResolution(640, 0, 1920, 1080);
    EXPECT_EQ(resolved.width_, 640);
    EXPECT_EQ(resolved.height_, 360);

    // 4:3 render — the clip-preset regression this derivation exists for:
    // a non-16:9 creation must not be squished to the 16:9 preset height.
    resolved = deriveCaptureOutputResolution(640, 0, 800, 600);
    EXPECT_EQ(resolved.width_, 640);
    EXPECT_EQ(resolved.height_, 480);
}

TEST(CaptureOutputResolutionTest, HeightOnlyDerivesWidthFromRenderAspect) {
    const CaptureOutputResolution resolved = deriveCaptureOutputResolution(0, 360, 1920, 1080);
    EXPECT_EQ(resolved.width_, 640);
    EXPECT_EQ(resolved.height_, 360);
}

TEST(CaptureOutputResolutionTest, OddDerivedDimensionRoundsDownToEven) {
    // 100 * 333 / 1000 == 33.3 -> 33 (odd) before the even mask.
    const CaptureOutputResolution resolved = deriveCaptureOutputResolution(100, 0, 1000, 333);
    EXPECT_EQ(resolved.width_, 100);
    EXPECT_EQ(resolved.height_, 32);
}

} // namespace
