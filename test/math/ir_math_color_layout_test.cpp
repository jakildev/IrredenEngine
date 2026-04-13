#include <gtest/gtest.h>
#include <irreden/ir_math.hpp>

#include <cmath>

namespace {

constexpr float kTolerance = 1e-4f;

// ─────────────────────────────────────────────
// Color::toPackedRGBA
// ─────────────────────────────────────────────

TEST(ColorPackTest, BlackFullAlpha) {
    // R=0, G=0, B=0, A=0xFF → packed: bits[31:24]=A, [23:16]=B, [15:8]=G, [7:0]=R
    constexpr IRMath::Color black{0, 0, 0, 255};
    EXPECT_EQ(black.toPackedRGBA(), 0xFF000000u);
}

TEST(ColorPackTest, WhiteFullAlpha) {
    constexpr IRMath::Color white{255, 255, 255, 255};
    EXPECT_EQ(white.toPackedRGBA(), 0xFFFFFFFFu);
}

TEST(ColorPackTest, ChannelOrdering) {
    // R=1, G=2, B=3, A=4: 1 | (2<<8) | (3<<16) | (4<<24)
    constexpr IRMath::Color c{1, 2, 3, 4};
    EXPECT_EQ(c.toPackedRGBA(), 0x04030201u);
}

TEST(ColorPackTest, TransparentBlackIsZero) {
    constexpr IRMath::Color invisible{0, 0, 0, 0};
    EXPECT_EQ(invisible.toPackedRGBA(), 0u);
}

// ─────────────────────────────────────────────
// colorHSVToColor — primary colors
// Hue is normalised [0, 1). 0=red, 1/3=green, 2/3=blue.
// ─────────────────────────────────────────────

TEST(ColorHSVToColorTest, PureRed) {
    IRMath::Color c = IRMath::colorHSVToColor({0.0f, 1.0f, 1.0f, 1.0f});
    EXPECT_EQ(c.red_, 255u);
    EXPECT_LE(c.green_, 1u);
    EXPECT_LE(c.blue_, 1u);
    EXPECT_EQ(c.alpha_, 255u);
}

TEST(ColorHSVToColorTest, PureGreen) {
    IRMath::Color c = IRMath::colorHSVToColor({1.0f / 3.0f, 1.0f, 1.0f, 1.0f});
    EXPECT_LE(c.red_, 1u);
    EXPECT_EQ(c.green_, 255u);
    EXPECT_LE(c.blue_, 1u);
}

TEST(ColorHSVToColorTest, PureBlue) {
    IRMath::Color c = IRMath::colorHSVToColor({2.0f / 3.0f, 1.0f, 1.0f, 1.0f});
    EXPECT_LE(c.red_, 1u);
    EXPECT_LE(c.green_, 1u);
    EXPECT_EQ(c.blue_, 255u);
}

TEST(ColorHSVToColorTest, WhiteIsZeroSaturation) {
    IRMath::Color c = IRMath::colorHSVToColor({0.0f, 0.0f, 1.0f, 1.0f});
    EXPECT_EQ(c.red_, 255u);
    EXPECT_EQ(c.green_, 255u);
    EXPECT_EQ(c.blue_, 255u);
}

TEST(ColorHSVToColorTest, BlackIsZeroValue) {
    IRMath::Color c = IRMath::colorHSVToColor({0.5f, 1.0f, 0.0f, 1.0f});
    EXPECT_EQ(c.red_, 0u);
    EXPECT_EQ(c.green_, 0u);
    EXPECT_EQ(c.blue_, 0u);
}

TEST(ColorHSVToColorTest, AlphaPassthrough) {
    // alpha=0.5 → 128 ± 1 byte
    IRMath::Color c = IRMath::colorHSVToColor({0.0f, 0.0f, 0.0f, 0.5f});
    EXPECT_NEAR(static_cast<float>(c.alpha_), 128.0f, 1.0f);
}

// ─────────────────────────────────────────────
// colorToColorHSV — round-trip
// ─────────────────────────────────────────────

TEST(ColorToColorHSVTest, RedHasCorrectHSV) {
    IRMath::ColorHSV hsv = IRMath::colorToColorHSV({255, 0, 0, 255});
    // Hue wraps at 0/1; distance to 0 from either direction
    float hueDist = std::min(hsv.hue_, 1.0f - hsv.hue_);
    EXPECT_NEAR(hueDist, 0.0f, 0.01f);
    EXPECT_NEAR(hsv.saturation_, 1.0f, 0.01f);
    EXPECT_NEAR(hsv.value_, 1.0f, 0.01f);
}

TEST(ColorToColorHSVTest, WhiteHasZeroSaturation) {
    IRMath::ColorHSV hsv = IRMath::colorToColorHSV({255, 255, 255, 255});
    EXPECT_NEAR(hsv.saturation_, 0.0f, 0.01f);
    EXPECT_NEAR(hsv.value_, 1.0f, 0.01f);
}

TEST(ColorToColorHSVTest, RedRoundTrip) {
    IRMath::Color original{255, 0, 0, 255};
    IRMath::Color back = IRMath::colorHSVToColor(IRMath::colorToColorHSV(original));
    EXPECT_NEAR(back.red_, original.red_, 1);
    EXPECT_NEAR(back.green_, original.green_, 1);
    EXPECT_NEAR(back.blue_, original.blue_, 1);
    EXPECT_NEAR(back.alpha_, original.alpha_, 1);
}

TEST(ColorToColorHSVTest, MidGrayRoundTrip) {
    IRMath::Color original{128, 128, 128, 200};
    IRMath::Color back = IRMath::colorHSVToColor(IRMath::colorToColorHSV(original));
    EXPECT_NEAR(back.red_, original.red_, 1);
    EXPECT_NEAR(back.green_, original.green_, 1);
    EXPECT_NEAR(back.blue_, original.blue_, 1);
    EXPECT_NEAR(back.alpha_, original.alpha_, 1);
}

// ─────────────────────────────────────────────
// applyHSVOffset
// ─────────────────────────────────────────────

TEST(ApplyHSVOffsetTest, ZeroOffsetIsIdentity) {
    IRMath::Color red{255, 0, 0, 255};
    IRMath::Color result = IRMath::applyHSVOffset(red, {0.0f, 0.0f, 0.0f, 0.0f});
    EXPECT_NEAR(result.red_, 255, 1);
    EXPECT_NEAR(result.green_, 0, 1);
    EXPECT_NEAR(result.blue_, 0, 1);
}

TEST(ApplyHSVOffsetTest, HueShiftRedToGreen) {
    // Red (hue≈0) + hue offset 1/3 → green (hue≈1/3)
    IRMath::Color red{255, 0, 0, 255};
    IRMath::Color result = IRMath::applyHSVOffset(red, {1.0f / 3.0f, 0.0f, 0.0f, 0.0f});
    EXPECT_LE(result.red_, 1u);
    EXPECT_GE(result.green_, 254u);
    EXPECT_LE(result.blue_, 1u);
}

TEST(ApplyHSVOffsetTest, NegativeSaturationGivesEqualChannels) {
    // Red + (-1.0 sat) → sat clamped to 0 → all RGB channels equal
    IRMath::Color red{255, 0, 0, 255};
    IRMath::Color result = IRMath::applyHSVOffset(red, {0.0f, -1.0f, 0.0f, 0.0f});
    EXPECT_EQ(result.red_, result.green_);
    EXPECT_EQ(result.green_, result.blue_);
}

// ─────────────────────────────────────────────
// layoutGridCentered
// ─────────────────────────────────────────────

TEST(LayoutGridTest, SingleItemAtOrigin) {
    auto p = IRMath::layoutGridCentered(0, 1, 1, 1.0f, 1.0f);
    EXPECT_NEAR(p.x, 0.0f, kTolerance);
    EXPECT_NEAR(p.y, 0.0f, kTolerance);
    EXPECT_NEAR(p.z, 0.0f, kTolerance);
}

TEST(LayoutGridTest, TwoByTwoCenteredAtOrigin) {
    // Sum of all 4 positions must be (0, 0, 0) — the grid is centered.
    IRMath::vec3 sum{0.0f};
    for (int i = 0; i < 4; ++i) {
        sum += IRMath::layoutGridCentered(i, 4, 2, 1.0f, 1.0f);
    }
    EXPECT_NEAR(sum.x, 0.0f, kTolerance);
    EXPECT_NEAR(sum.y, 0.0f, kTolerance);
}

TEST(LayoutGridTest, SpacingScalesPositions) {
    // Adjacent items in a 1-row, 2-column grid: X should differ by spacingPrimary.
    auto p0 = IRMath::layoutGridCentered(0, 2, 2, 4.0f, 1.0f);
    auto p1 = IRMath::layoutGridCentered(1, 2, 2, 4.0f, 1.0f);
    EXPECT_NEAR(std::abs(p1.x - p0.x), 4.0f, kTolerance);
}

TEST(LayoutGridTest, DepthPassedToZ) {
    auto p = IRMath::layoutGridCentered(0, 1, 1, 1.0f, 1.0f, IRMath::PlaneIso::XY, 7.0f);
    EXPECT_NEAR(p.z, 7.0f, kTolerance);
}

// ─────────────────────────────────────────────
// layoutCircle
// ─────────────────────────────────────────────

TEST(LayoutCircleTest, AllPointsAtRadius) {
    const float radius = 5.0f;
    for (int i = 0; i < 8; ++i) {
        auto p = IRMath::layoutCircle(i, 8, radius);
        float dist = std::sqrt(p.x * p.x + p.y * p.y);
        EXPECT_NEAR(dist, radius, kTolerance) << "index=" << i;
    }
}

TEST(LayoutCircleTest, FirstPointAtDefaultAngle) {
    // Default startAngle = -π/2 → cos=0, sin=-1 → (0, -1, 0)
    auto p = IRMath::layoutCircle(0, 4, 1.0f);
    EXPECT_NEAR(p.x, 0.0f, kTolerance);
    EXPECT_NEAR(p.y, -1.0f, kTolerance);
    EXPECT_NEAR(p.z, 0.0f, kTolerance);
}

TEST(LayoutCircleTest, SumOfEvenlySpacedIsNearZero) {
    // Evenly-spaced points on a circle sum to zero.
    IRMath::vec3 sum{0.0f};
    for (int i = 0; i < 6; ++i) {
        sum += IRMath::layoutCircle(i, 6, 3.0f);
    }
    EXPECT_NEAR(sum.x, 0.0f, kTolerance);
    EXPECT_NEAR(sum.y, 0.0f, kTolerance);
}

// ─────────────────────────────────────────────
// layoutSquareSpiral
// ─────────────────────────────────────────────

TEST(LayoutSquareSpiralTest, IndexZeroIsOrigin) {
    auto p = IRMath::layoutSquareSpiral(0, 1.0f);
    EXPECT_NEAR(p.x, 0.0f, kTolerance);
    EXPECT_NEAR(p.y, 0.0f, kTolerance);
}

TEST(LayoutSquareSpiralTest, IndexOneIsFirstStep) {
    // Spiral starts moving +X, so index 1 is at (spacing, 0).
    auto p = IRMath::layoutSquareSpiral(1, 1.0f);
    EXPECT_NEAR(p.x, 1.0f, kTolerance);
    EXPECT_NEAR(p.y, 0.0f, kTolerance);
}

TEST(LayoutSquareSpiralTest, SpacingScales) {
    auto p = IRMath::layoutSquareSpiral(1, 3.0f);
    EXPECT_NEAR(p.x, 3.0f, kTolerance);
    EXPECT_NEAR(p.y, 0.0f, kTolerance);
}

} // namespace
