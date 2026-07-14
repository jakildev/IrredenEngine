#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>

namespace {

constexpr float kTolerance = 1e-5f;

// --- wrapToRange -----------------------------------------------------------

TEST(IRMathWrapToRange, InRangePositiveIsIdentity) {
    EXPECT_NEAR(IRMath::wrapToRange(1.5f, IRMath::kTwoPi), 1.5f, kTolerance);
    EXPECT_NEAR(IRMath::wrapToRange(0.0f, IRMath::kTwoPi), 0.0f, kTolerance);
}

TEST(IRMathWrapToRange, NegativeInputLandsInRange) {
    const float wrapped = IRMath::wrapToRange(-1.0f, IRMath::kTwoPi);
    EXPECT_GE(wrapped, 0.0f);
    EXPECT_LT(wrapped, IRMath::kTwoPi);
    EXPECT_NEAR(wrapped, IRMath::kTwoPi - 1.0f, kTolerance);
}

TEST(IRMathWrapToRange, MultiCycleInputWrapsToSinglePeriod) {
    const float wrapped = IRMath::wrapToRange(IRMath::kTwoPi * 3.0f + 0.5f, IRMath::kTwoPi);
    EXPECT_GE(wrapped, 0.0f);
    EXPECT_LT(wrapped, IRMath::kTwoPi);
    EXPECT_NEAR(wrapped, 0.5f, 1e-3f);
}

TEST(IRMathWrapToRange, ExactRangeFoldsToZero) {
    EXPECT_NEAR(IRMath::wrapToRange(IRMath::kTwoPi, IRMath::kTwoPi), 0.0f, kTolerance);
}

// A tiny-negative input rounds to exactly `range` after the +range correction
// in float; the fold-back must keep the result strictly inside [0, range).
TEST(IRMathWrapToRange, TinyNegativeStaysStrictlyBelowRange) {
    const float wrapped = IRMath::wrapToRange(-1e-10f, IRMath::kTwoPi);
    EXPECT_GE(wrapped, 0.0f);
    EXPECT_LT(wrapped, IRMath::kTwoPi);
}

// Sites 3/4 wrap `double` periods; routing them through float would drift as
// elapsedSeconds_ grows, so the template must instantiate on double.
TEST(IRMathWrapToRange, DoubleInstantiationPreservesPrecision) {
    const double wrapped = IRMath::wrapToRange(1e6 + 0.25, 1.0);
    EXPECT_NEAR(wrapped, 0.25, 1e-9);
}

// --- wrapAngleTwoPi --------------------------------------------------------

TEST(IRMathWrapAngleTwoPi, NegativeAngleWrapsPositive) {
    const float wrapped = IRMath::wrapAngleTwoPi(-1.0f);
    EXPECT_GE(wrapped, 0.0f);
    EXPECT_LT(wrapped, IRMath::kTwoPi);
    EXPECT_NEAR(wrapped, IRMath::kTwoPi - 1.0f, kTolerance);
}

TEST(IRMathWrapAngleTwoPi, TwoPiWrapsToZero) {
    EXPECT_NEAR(IRMath::wrapAngleTwoPi(IRMath::kTwoPi), 0.0f, kTolerance);
}

TEST(IRMathWrapAngleTwoPi, ManyCycleInput) {
    const float wrapped = IRMath::wrapAngleTwoPi(IRMath::kTwoPi * 5.0f + 1.0f);
    EXPECT_NEAR(wrapped, 1.0f, 1e-3f);
}

// --- wrapAnglePi -----------------------------------------------------------

TEST(IRMathWrapAnglePi, ZeroIsIdentity) {
    EXPECT_NEAR(IRMath::wrapAnglePi(0.0f), 0.0f, kTolerance);
}

// [-π, π) is right-half-open: +π maps to -π (same physical angle mod 2π).
TEST(IRMathWrapAnglePi, PositivePiMapsToNegativePi) {
    EXPECT_NEAR(IRMath::wrapAnglePi(IRMath::kPi), -IRMath::kPi, kTolerance);
}

TEST(IRMathWrapAnglePi, NegativePiStaysNegativePi) {
    EXPECT_NEAR(IRMath::wrapAnglePi(-IRMath::kPi), -IRMath::kPi, kTolerance);
}

TEST(IRMathWrapAnglePi, ThreePiMapsToNegativePi) {
    EXPECT_NEAR(IRMath::wrapAnglePi(3.0f * IRMath::kPi), -IRMath::kPi, kTolerance);
}

TEST(IRMathWrapAnglePi, JustBelowPiStaysPositive) {
    const float wrapped = IRMath::wrapAnglePi(IRMath::kPi - 0.01f);
    EXPECT_GT(wrapped, 0.0f);
    EXPECT_LT(wrapped, IRMath::kPi);
    EXPECT_NEAR(wrapped, IRMath::kPi - 0.01f, kTolerance);
}

} // namespace
