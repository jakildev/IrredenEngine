#include <gtest/gtest.h>
#include <irreden/ir_math.hpp>

namespace {

constexpr float kTolerance = 1e-5f;

// Returns the HSV vec3 for a Color (H in [0,360), S and V in [0,1]).
auto hsvOf = [](const IRMath::Color &c) {
    return glm::hsvColor(glm::vec3(c.red_ / 255.0f, c.green_ / 255.0f, c.blue_ / 255.0f));
};

// ─────────────────────────────────────────────
// kEasingFunctions map
// ─────────────────────────────────────────────

TEST(EasingMapTest, ContainsAllEnumValues) {
    // The enum runs kLinearInterpolation..kBounceEaseInOut (31 values).
    EXPECT_EQ(IRMath::kEasingFunctions.size(), 31u);
}

TEST(EasingMapTest, LinearInterpolationIsLinear) {
    auto f = IRMath::kEasingFunctions.at(IRMath::kLinearInterpolation);
    EXPECT_NEAR(f(0.0f), 0.0f, kTolerance);
    EXPECT_NEAR(f(0.5f), 0.5f, kTolerance);
    EXPECT_NEAR(f(1.0f), 1.0f, kTolerance);
}

TEST(EasingMapTest, AllFunctionsBoundaryConditions) {
    // Every easing function must satisfy f(0)=0 and f(1)=1.
    for (auto &[key, fn] : IRMath::kEasingFunctions) {
        EXPECT_NEAR(fn(0.0f), 0.0f, kTolerance) << "f(0) != 0 for enum value " << key;
        EXPECT_NEAR(fn(1.0f), 1.0f, kTolerance) << "f(1) != 1 for enum value " << key;
    }
}

TEST(EasingMapTest, QuadraticEaseInIsSlowAtStart) {
    // Ease-in: progress at t=0.5 should be below the linear midpoint.
    auto f = IRMath::kEasingFunctions.at(IRMath::kQuadraticEaseIn);
    EXPECT_LT(f(0.5f), 0.5f);
}

TEST(EasingMapTest, QuadraticEaseOutIsFastAtStart) {
    // Ease-out: progress at t=0.5 should be above the linear midpoint.
    auto f = IRMath::kEasingFunctions.at(IRMath::kQuadraticEaseOut);
    EXPECT_GT(f(0.5f), 0.5f);
}

TEST(EasingMapTest, QuadraticEaseInOutIsSymmetric) {
    // Ease-in-out: midpoint should be exactly 0.5 (point symmetry).
    auto f = IRMath::kEasingFunctions.at(IRMath::kQuadraticEaseInOut);
    EXPECT_NEAR(f(0.5f), 0.5f, kTolerance);
}

TEST(EasingMapTest, EaseInIsMonotonicallyIncreasing) {
    // Cubic ease-in must be non-decreasing on [0, 1].
    auto f = IRMath::kEasingFunctions.at(IRMath::kCubicEaseIn);
    float prev = f(0.0f);
    for (int i = 1; i <= 20; ++i) {
        float t = static_cast<float>(i) / 20.0f;
        float cur = f(t);
        EXPECT_GE(cur, prev - kTolerance) << "cubic ease-in decreased at t=" << t;
        prev = cur;
    }
}

// ─────────────────────────────────────────────
// sortByHue / sortBySaturation / sortByValue / sortByLuminance
// ─────────────────────────────────────────────

TEST(ColorSortTest, SortByHueOrdersRedGreenBlue) {
    // Red hue≈0°, green≈120°, blue≈240° — after sort: R, G, B
    std::vector<IRMath::Color> colors = {
        {0, 0, 255, 255}, // blue
        {255, 0, 0, 255}, // red
        {0, 255, 0, 255}, // green
    };
    auto sorted = IRMath::sortByHue(colors);
    // Hue of sorted[0] ≤ sorted[1] ≤ sorted[2]
    EXPECT_LE(hsvOf(sorted[0]).x, hsvOf(sorted[1]).x);
    EXPECT_LE(hsvOf(sorted[1]).x, hsvOf(sorted[2]).x);
    // Specifically: red should be first, blue should be last
    EXPECT_EQ(sorted[0].red_, 255u);
    EXPECT_EQ(sorted[2].blue_, 255u);
}

TEST(ColorSortTest, SortBySaturationLowToHigh) {
    // Gray (sat≈0) should sort before fully-saturated red (sat≈1).
    std::vector<IRMath::Color> colors = {
        {255, 0, 0, 255},     // pure red, sat=1
        {200, 200, 200, 255}, // light gray, sat≈0
        {128, 64, 64, 255},   // muted, sat≈0.5
    };
    auto sorted = IRMath::sortBySaturation(colors);
    EXPECT_LE(hsvOf(sorted[0]).y, hsvOf(sorted[1]).y);
    EXPECT_LE(hsvOf(sorted[1]).y, hsvOf(sorted[2]).y);
}

TEST(ColorSortTest, SortByValueLowToHigh) {
    // Black (val=0) < dark (val≈0.5) < white (val=1).
    std::vector<IRMath::Color> colors = {
        {255, 255, 255, 255}, // white, val=1
        {0, 0, 0, 255},       // black, val=0
        {128, 128, 128, 255}, // gray, val≈0.5
    };
    auto sorted = IRMath::sortByValue(colors);
    EXPECT_LE(hsvOf(sorted[0]).z, hsvOf(sorted[1]).z);
    EXPECT_LE(hsvOf(sorted[1]).z, hsvOf(sorted[2]).z);
    EXPECT_EQ(sorted[0].red_, 0u);   // black first
    EXPECT_EQ(sorted[2].red_, 255u); // white last
}

TEST(ColorSortTest, SortByLuminanceLowToHigh) {
    // Luminance = 0.299R + 0.587G + 0.114B
    // Black < red (lum≈76) < green (lum≈150) < white (lum≈255)
    std::vector<IRMath::Color> colors = {
        {0, 255, 0, 255},   // green ~150
        {255, 255, 255, 255}, // white ~255
        {0, 0, 0, 255},     // black 0
        {255, 0, 0, 255},   // red ~76
    };
    auto sorted = IRMath::sortByLuminance(colors);
    auto lum = [](const IRMath::Color &c) {
        return 0.299f * c.red_ + 0.587f * c.green_ + 0.114f * c.blue_;
    };
    for (std::size_t i = 1; i < sorted.size(); ++i) {
        EXPECT_LE(lum(sorted[i - 1]), lum(sorted[i])) << "order broken at i=" << i;
    }
}

TEST(ColorSortTest, SingleElementUnchanged) {
    std::vector<IRMath::Color> single = {{100, 50, 200, 255}};
    EXPECT_EQ(IRMath::sortByHue(single).front().red_, 100u);
    EXPECT_EQ(IRMath::sortBySaturation(single).front().green_, 50u);
    EXPECT_EQ(IRMath::sortByValue(single).front().blue_, 200u);
}

TEST(ColorSortTest, EmptyVectorReturnsEmpty) {
    std::vector<IRMath::Color> empty;
    EXPECT_TRUE(IRMath::sortByHue(empty).empty());
    EXPECT_TRUE(IRMath::sortBySaturation(empty).empty());
    EXPECT_TRUE(IRMath::sortByValue(empty).empty());
    EXPECT_TRUE(IRMath::sortByLuminance(empty).empty());
}

TEST(ColorSortTest, AlreadySortedByHueIsStable) {
    std::vector<IRMath::Color> ordered = {
        {255, 0, 0, 255},   // red  hue≈0
        {0, 255, 0, 255},   // green hue≈120
        {0, 0, 255, 255},   // blue  hue≈240
    };
    auto result = IRMath::sortByHue(ordered);
    EXPECT_EQ(result[0].red_, 255u);
    EXPECT_EQ(result[1].green_, 255u);
    EXPECT_EQ(result[2].blue_, 255u);
}

} // namespace
