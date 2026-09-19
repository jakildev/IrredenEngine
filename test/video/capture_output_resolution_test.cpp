#include <gtest/gtest.h>

#include <irreden/video/ir_video_types.hpp>

// Coverage for `resolveCaptureOutputResolution` — the pure aspect-derivation
// step behind `VideoManager::configureCaptureOutputResolution` /
// `toggleCapture`. Pure by construction (no render manager, no GPU), so the
// interesting cases are pinned at compile time too.

namespace {

using IRVideo::CaptureOutputResolution;
using IRVideo::resolveCaptureOutputResolution;

constexpr CaptureOutputResolution kBothUnset = resolveCaptureOutputResolution(0, 0, 1280, 720);
static_assert(kBothUnset.width_ == 1280 && kBothUnset.height_ == 720);

constexpr CaptureOutputResolution kBothSet = resolveCaptureOutputResolution(640, 360, 1280, 720);
static_assert(kBothSet.width_ == 640 && kBothSet.height_ == 360);

// Odd set dimensions round down to even.
constexpr CaptureOutputResolution kBothSetOdd = resolveCaptureOutputResolution(641, 361, 1280, 720);
static_assert(kBothSetOdd.width_ == 640 && kBothSetOdd.height_ == 360);

// 16:9 render, width-only override: height derives exactly, already even.
constexpr CaptureOutputResolution kWidthOnly16x9 =
    resolveCaptureOutputResolution(640, 0, 1280, 720);
static_assert(kWidthOnly16x9.width_ == 640 && kWidthOnly16x9.height_ == 360);

// Non-16:9 render (4:3), width-only and height-only override.
constexpr CaptureOutputResolution kWidthOnly4x3 = resolveCaptureOutputResolution(640, 0, 800, 600);
static_assert(kWidthOnly4x3.width_ == 640 && kWidthOnly4x3.height_ == 480);

constexpr CaptureOutputResolution kHeightOnly4x3 = resolveCaptureOutputResolution(0, 480, 800, 600);
static_assert(kHeightOnly4x3.width_ == 640 && kHeightOnly4x3.height_ == 480);

// Raw quotient 61 rounds down to even.
constexpr CaptureOutputResolution kOddQuotient = resolveCaptureOutputResolution(110, 0, 1280, 720);
static_assert(kOddQuotient.width_ == 110 && kOddQuotient.height_ == 60);

// A negative override is treated as unset.
constexpr CaptureOutputResolution kNegativeIsUnset =
    resolveCaptureOutputResolution(-5, 360, 1280, 720);
static_assert(kNegativeIsUnset.width_ == 640 && kNegativeIsUnset.height_ == 360);

// Degenerate set dimensions floor at 2 so the encoder never sees a
// zero-sized dimension, on both the set and the derived axis.
constexpr CaptureOutputResolution kFloorSetDimension =
    resolveCaptureOutputResolution(2, 0, 1280, 720);
static_assert(kFloorSetDimension.width_ == 2 && kFloorSetDimension.height_ == 2);

constexpr CaptureOutputResolution kFloorOddSetDimension =
    resolveCaptureOutputResolution(1, 0, 1280, 720);
static_assert(kFloorOddSetDimension.width_ == 2 && kFloorOddSetDimension.height_ == 2);

constexpr CaptureOutputResolution kFloorBothSetDimensions =
    resolveCaptureOutputResolution(1, 1, 1280, 720);
static_assert(kFloorBothSetDimensions.width_ == 2 && kFloorBothSetDimensions.height_ == 2);

// An unreported render size (0x0 before the first frame or while minimized)
// has no aspect: the derived axis mirrors the set one rather than dividing
// by zero, and a single zero dimension is just as aspect-less as both.
constexpr CaptureOutputResolution kWidthOnlyNoRenderSize =
    resolveCaptureOutputResolution(640, 0, 0, 0);
static_assert(kWidthOnlyNoRenderSize.width_ == 640 && kWidthOnlyNoRenderSize.height_ == 640);

constexpr CaptureOutputResolution kHeightOnlyNoRenderSize =
    resolveCaptureOutputResolution(0, 480, 0, 0);
static_assert(kHeightOnlyNoRenderSize.width_ == 480 && kHeightOnlyNoRenderSize.height_ == 480);

constexpr CaptureOutputResolution kWidthOnlyZeroRenderWidth =
    resolveCaptureOutputResolution(640, 0, 0, 720);
static_assert(kWidthOnlyZeroRenderWidth.width_ == 640 && kWidthOnlyZeroRenderWidth.height_ == 640);

constexpr CaptureOutputResolution kHeightOnlyZeroRenderHeight =
    resolveCaptureOutputResolution(0, 480, 1280, 0);
static_assert(
    kHeightOnlyZeroRenderHeight.width_ == 480 && kHeightOnlyZeroRenderHeight.height_ == 480
);

TEST(CaptureOutputResolutionTest, BothUnsetFollowsRenderOutputUnrounded) {
    const CaptureOutputResolution resolved = resolveCaptureOutputResolution(0, 0, 1280, 720);
    EXPECT_EQ(resolved.width_, 1280);
    EXPECT_EQ(resolved.height_, 720);
}

TEST(CaptureOutputResolutionTest, BothSetKeepsCallerDimensionsRoundedEven) {
    CaptureOutputResolution resolved = resolveCaptureOutputResolution(640, 360, 1280, 720);
    EXPECT_EQ(resolved.width_, 640);
    EXPECT_EQ(resolved.height_, 360);

    resolved = resolveCaptureOutputResolution(641, 361, 1280, 720);
    EXPECT_EQ(resolved.width_, 640);
    EXPECT_EQ(resolved.height_, 360);
}

TEST(CaptureOutputResolutionTest, WidthOnlyDerivesHeightFromRenderAspect) {
    // 16:9 render.
    CaptureOutputResolution resolved = resolveCaptureOutputResolution(640, 0, 1280, 720);
    EXPECT_EQ(resolved.width_, 640);
    EXPECT_EQ(resolved.height_, 360);

    // 4:3 render — the clip-preset regression this derivation exists for:
    // a non-16:9 creation must not be squished to the 16:9 preset height.
    resolved = resolveCaptureOutputResolution(640, 0, 800, 600);
    EXPECT_EQ(resolved.width_, 640);
    EXPECT_EQ(resolved.height_, 480);
}

TEST(CaptureOutputResolutionTest, HeightOnlyDerivesWidthFromRenderAspect) {
    const CaptureOutputResolution resolved = resolveCaptureOutputResolution(0, 480, 800, 600);
    EXPECT_EQ(resolved.width_, 640);
    EXPECT_EQ(resolved.height_, 480);
}

TEST(CaptureOutputResolutionTest, OddDerivedQuotientRoundsDownToEven) {
    // 110 * 720 / 1280 == 61.875 -> 61 (odd) before the even mask.
    const CaptureOutputResolution resolved = resolveCaptureOutputResolution(110, 0, 1280, 720);
    EXPECT_EQ(resolved.width_, 110);
    EXPECT_EQ(resolved.height_, 60);
}

TEST(CaptureOutputResolutionTest, NegativeOverrideIsTreatedAsUnset) {
    const CaptureOutputResolution resolved = resolveCaptureOutputResolution(-5, 360, 1280, 720);
    EXPECT_EQ(resolved.width_, 640);
    EXPECT_EQ(resolved.height_, 360);
}

TEST(CaptureOutputResolutionTest, DegenerateSetDimensionFloorsAtTwoOnBothAxes) {
    CaptureOutputResolution resolved = resolveCaptureOutputResolution(2, 0, 1280, 720);
    EXPECT_EQ(resolved.width_, 2);
    EXPECT_EQ(resolved.height_, 2);

    resolved = resolveCaptureOutputResolution(1, 0, 1280, 720);
    EXPECT_EQ(resolved.width_, 2);
    EXPECT_EQ(resolved.height_, 2);

    resolved = resolveCaptureOutputResolution(1, 1, 1280, 720);
    EXPECT_EQ(resolved.width_, 2);
    EXPECT_EQ(resolved.height_, 2);
}

TEST(CaptureOutputResolutionTest, UnreportedRenderSizeDerivesSquareInsteadOfDividingByZero) {
    // The render manager's viewport is 0x0 until the window reports a size
    // and while minimized, and toggleCapture's viewport fallback is the same
    // pair — so a single-dimension override must resolve without a division.
    CaptureOutputResolution resolved = resolveCaptureOutputResolution(640, 0, 0, 0);
    EXPECT_EQ(resolved.width_, 640);
    EXPECT_EQ(resolved.height_, 640);

    resolved = resolveCaptureOutputResolution(0, 480, 0, 0);
    EXPECT_EQ(resolved.width_, 480);
    EXPECT_EQ(resolved.height_, 480);

    // One zero dimension is as aspect-less as both: never derive against the
    // surviving one.
    resolved = resolveCaptureOutputResolution(640, 0, 0, 720);
    EXPECT_EQ(resolved.width_, 640);
    EXPECT_EQ(resolved.height_, 640);

    resolved = resolveCaptureOutputResolution(0, 480, 1280, 0);
    EXPECT_EQ(resolved.width_, 480);
    EXPECT_EQ(resolved.height_, 480);

    // Both unset still passes the render pair through untouched.
    resolved = resolveCaptureOutputResolution(0, 0, 0, 0);
    EXPECT_EQ(resolved.width_, 0);
    EXPECT_EQ(resolved.height_, 0);
}

} // namespace
