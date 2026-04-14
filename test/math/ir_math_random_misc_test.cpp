#include <gtest/gtest.h>
#include <irreden/ir_math.hpp>

#include <cstdlib>

namespace {

constexpr float kTolerance = 1e-5f;

// ─────────────────────────────────────────────
// randomInt / randomFloat / randomColor / randomVec
// ─────────────────────────────────────────────

TEST(RandomTest, RandomIntAlwaysInRange) {
    std::srand(42);
    for (int i = 0; i < 1000; ++i) {
        int v = IRMath::randomInt(10, 20);
        EXPECT_GE(v, 10) << "randomInt below min at i=" << i;
        EXPECT_LE(v, 20) << "randomInt above max at i=" << i;
    }
}

TEST(RandomTest, RandomIntMinEqualsMaxReturnsThatValue) {
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(IRMath::randomInt(7, 7), 7);
    }
}

TEST(RandomTest, RandomFloatAlwaysInRange) {
    std::srand(42);
    for (int i = 0; i < 1000; ++i) {
        float v = IRMath::randomFloat(0.5f, 2.5f);
        EXPECT_GE(v, 0.5f) << "randomFloat below min at i=" << i;
        EXPECT_LE(v, 2.5f) << "randomFloat above max at i=" << i;
    }
}

TEST(RandomTest, RandomColorHasAlpha255) {
    for (int i = 0; i < 50; ++i) {
        auto c = IRMath::randomColor();
        EXPECT_EQ(c.alpha_, 255u) << "alpha was not 255 at i=" << i;
    }
}

TEST(RandomTest, RandomColorFromEmptyPaletteStillValid) {
    std::vector<IRMath::Color> empty;
    // Falls through to randomColor() — alpha is always 255.
    auto c = IRMath::randomColor(empty);
    EXPECT_EQ(c.alpha_, 255u);
}

TEST(RandomTest, RandomColorFromPaletteReturnsPaletteEntry) {
    std::vector<IRMath::Color> palette = {
        {10, 20, 30, 255},
        {40, 50, 60, 255},
        {70, 80, 90, 255},
    };
    for (int i = 0; i < 200; ++i) {
        auto c = IRMath::randomColor(palette);
        auto it = std::find_if(palette.begin(), palette.end(), [&c](const IRMath::Color &p) {
            return c.red_ == p.red_ && c.green_ == p.green_ && c.blue_ == p.blue_;
        });
        EXPECT_NE(it, palette.end()) << "color not from palette at i=" << i;
    }
}

TEST(RandomTest, RandomVecComponentsInRange) {
    IRMath::vec3 lo(1.0f, 2.0f, 3.0f);
    IRMath::vec3 hi(4.0f, 5.0f, 6.0f);
    std::srand(42);
    for (int i = 0; i < 200; ++i) {
        auto v = IRMath::randomVec(lo, hi);
        EXPECT_GE(v.x, lo.x);
        EXPECT_LE(v.x, hi.x);
        EXPECT_GE(v.y, lo.y);
        EXPECT_LE(v.y, hi.y);
        EXPECT_GE(v.z, lo.z);
        EXPECT_LE(v.z, hi.z);
    }
}

// ─────────────────────────────────────────────
// layoutZigZagCentered
// ─────────────────────────────────────────────

TEST(LayoutZigZagTest, ZeroCountReturnsOrigin) {
    // count=0: clamp(index, 0, -1) is undefined; the guard returns origin.
    auto r = IRMath::layoutZigZagCentered(0, 0, 2, 1.0f, 1.0f);
    EXPECT_NEAR(r.x, 0.0f, kTolerance);
    EXPECT_NEAR(r.y, 0.0f, kTolerance);
}

TEST(LayoutZigZagTest, FourItemsTwoPerZagCenteredXY) {
    // 4 items, 2 per zag, spacing=1.0, plane=XY (default)
    // Grid: 2 cols × 2 rows, cx=0.5, cy=0.5
    // Row 0 (even): index 0→col 0, index 1→col 1
    // Row 1 (odd, reversed): index 2→col 1, index 3→col 0
    const int N = 4;
    const int zag = 2;
    const float sp = 1.0f;

    auto p0 = IRMath::layoutZigZagCentered(0, N, zag, sp, sp);
    auto p1 = IRMath::layoutZigZagCentered(1, N, zag, sp, sp);
    auto p2 = IRMath::layoutZigZagCentered(2, N, zag, sp, sp);
    auto p3 = IRMath::layoutZigZagCentered(3, N, zag, sp, sp);

    // Row 0 goes left→right: p0.x < p1.x
    EXPECT_LT(p0.x, p1.x);
    // Row 1 goes right→left (zigzag reversal): p2.x > p3.x
    EXPECT_GT(p2.x, p3.x);
    // Rows are vertically separated: p2.y > p0.y
    EXPECT_GT(p2.y, p0.y);
    // Center of all four x-coordinates is ~0 (symmetric layout)
    float xMean = (p0.x + p1.x + p2.x + p3.x) / 4.0f;
    EXPECT_NEAR(xMean, 0.0f, kTolerance);
}

TEST(LayoutZigZagTest, SingleItemAtOrigin) {
    auto r = IRMath::layoutZigZagCentered(0, 1, 1, 1.0f, 1.0f);
    EXPECT_NEAR(r.x, 0.0f, kTolerance);
    EXPECT_NEAR(r.y, 0.0f, kTolerance);
}

TEST(LayoutZigZagTest, DepthPassedThrough) {
    auto r = IRMath::layoutZigZagCentered(0, 4, 2, 1.0f, 1.0f, IRMath::PlaneIso::XY, 7.0f);
    // PlaneIso::XY → depth maps to z
    EXPECT_NEAR(r.z, 7.0f, kTolerance);
}

// ─────────────────────────────────────────────
// layoutHelix
// ─────────────────────────────────────────────

TEST(LayoutHelixTest, RadiusIsConstantAlongHelix) {
    const float radius = 3.0f;
    const int count = 16;
    for (int i = 0; i < count; ++i) {
        auto p = IRMath::layoutHelix(i, count, radius, 2.0f, 4.0f, IRMath::CoordinateAxis::ZAxis);
        EXPECT_NEAR(IRMath::length(IRMath::vec2(p.x, p.y)), radius, 1e-4f) << "radius wrong at index=" << i;
    }
}

TEST(LayoutHelixTest, HeightSpanIsCoveredEndToEnd) {
    // First point z = -heightSpan/2; last point z = +heightSpan/2.
    const float heightSpan = 6.0f;
    const int count = 8;
    auto first = IRMath::layoutHelix(0, count, 1.0f, 1.0f, heightSpan, IRMath::CoordinateAxis::ZAxis);
    auto last  = IRMath::layoutHelix(count - 1, count, 1.0f, 1.0f, heightSpan, IRMath::CoordinateAxis::ZAxis);
    EXPECT_NEAR(first.z, -heightSpan / 2.0f, kTolerance);
    EXPECT_NEAR(last.z,  +heightSpan / 2.0f, kTolerance);
}

TEST(LayoutHelixTest, XAxisHelixPutsHeightOnX) {
    // For XAxis, height goes along x; radius is in y-z plane.
    const float radius = 2.0f;
    const float heightSpan = 4.0f;
    auto first = IRMath::layoutHelix(0, 4, radius, 1.0f, heightSpan, IRMath::CoordinateAxis::XAxis);
    auto last  = IRMath::layoutHelix(3, 4, radius, 1.0f, heightSpan, IRMath::CoordinateAxis::XAxis);
    EXPECT_NEAR(first.x, -heightSpan / 2.0f, kTolerance);
    EXPECT_NEAR(last.x,  +heightSpan / 2.0f, kTolerance);
    // radius in y-z plane
    EXPECT_NEAR(IRMath::length(IRMath::vec2(first.y, first.z)), radius, 1e-4f);
}

TEST(LayoutHelixTest, SingleCountReturnsStartPoint) {
    // With count=1, t=0, angle=0, c=1, s=0, h=-heightSpan/2.
    auto p = IRMath::layoutHelix(0, 1, 2.0f, 3.0f, 10.0f, IRMath::CoordinateAxis::ZAxis);
    EXPECT_NEAR(p.x, 2.0f, kTolerance);   // c*radius = 1*2
    EXPECT_NEAR(p.y, 0.0f, kTolerance);   // s*radius = 0*2
    EXPECT_NEAR(p.z, -5.0f, kTolerance);  // h = (0-0.5)*10 = -5
}

} // namespace
