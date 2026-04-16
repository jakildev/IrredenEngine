#include <gtest/gtest.h>
#include <irreden/math/physics.hpp>

namespace {

constexpr float kTolerance = 1e-5f;

TEST(PhysicsTest, ImpulseForHeightPositiveGravityAndHeight) {
    float g = 9.8f;
    float h = 5.0f;
    float v0 = IRMath::impulseForHeight(g, h);
    EXPECT_NEAR(v0, std::sqrt(2.0f * g * h), kTolerance);
}

TEST(PhysicsTest, ImpulseForHeightRoundTripsWithHeightForImpulse) {
    // heightForImpulse(g, impulseForHeight(g, h)) == h
    float g = 9.8f;
    float h = 3.5f;
    float v0 = IRMath::impulseForHeight(g, h);
    float recovered = IRMath::heightForImpulse(g, v0);
    EXPECT_NEAR(recovered, h, kTolerance);
}

TEST(PhysicsTest, HeightForImpulseRoundTripsWithImpulseForHeight) {
    // impulseForHeight(g, heightForImpulse(g, v)) == v
    float g = 9.8f;
    float v = 7.0f;
    float h = IRMath::heightForImpulse(g, v);
    float recovered = IRMath::impulseForHeight(g, h);
    EXPECT_NEAR(recovered, v, kTolerance);
}

TEST(PhysicsTest, HeightForImpulseFormula) {
    float g = 9.8f;
    float v = 4.0f;
    EXPECT_NEAR(IRMath::heightForImpulse(g, v), (v * v) / (2.0f * g), kTolerance);
}

TEST(PhysicsTest, ImpulseForHeightScalesWithSquareRoot) {
    // Doubling height multiplies impulse by sqrt(2)
    float g = 9.8f;
    float h = 2.0f;
    float v1 = IRMath::impulseForHeight(g, h);
    float v2 = IRMath::impulseForHeight(g, 2.0f * h);
    EXPECT_NEAR(v2 / v1, std::sqrt(2.0f), kTolerance);
}

TEST(PhysicsTest, ImpulseForZeroHeightIsZero) {
    EXPECT_NEAR(IRMath::impulseForHeight(9.8f, 0.0f), 0.0f, kTolerance);
}

TEST(PhysicsTest, FlightTimeForHeightFormula) {
    // t = 2 * sqrt(2h / g)
    float g = 9.8f;
    float h = 5.0f;
    float expected = 2.0f * std::sqrt(2.0f * h / g);
    EXPECT_NEAR(IRMath::flightTimeForHeight(g, h), expected, kTolerance);
}

TEST(PhysicsTest, FlightTimeForZeroHeightIsZero) {
    EXPECT_NEAR(IRMath::flightTimeForHeight(9.8f, 0.0f), 0.0f, kTolerance);
}

TEST(PhysicsTest, FlightTimeScalesWithSqrtHeight) {
    // Quadrupling height doubles flight time
    float g = 9.8f;
    float h = 1.0f;
    float t1 = IRMath::flightTimeForHeight(g, h);
    float t4 = IRMath::flightTimeForHeight(g, 4.0f * h);
    EXPECT_NEAR(t4 / t1, 2.0f, kTolerance);
}

TEST(PhysicsTest, FlightTimeConsistentWithImpulse) {
    // For a symmetric arc: t = 2 * v0 / g
    float g = 9.8f;
    float h = 4.0f;
    float v0 = IRMath::impulseForHeight(g, h);
    float t_expected = 2.0f * v0 / g;
    EXPECT_NEAR(IRMath::flightTimeForHeight(g, h), t_expected, kTolerance);
}

TEST(PhysicsTest, MaxFrameDisplacementBasic) {
    EXPECT_NEAR(IRMath::maxFrameDisplacement(10.0f, 0.016f), 10.0f * 0.016f, kTolerance);
}

TEST(PhysicsTest, MaxFrameDisplacementZeroVelocity) {
    EXPECT_NEAR(IRMath::maxFrameDisplacement(0.0f, 0.016f), 0.0f, kTolerance);
}

TEST(PhysicsTest, MaxFrameDisplacementZeroDt) {
    EXPECT_NEAR(IRMath::maxFrameDisplacement(100.0f, 0.0f), 0.0f, kTolerance);
}

TEST(PhysicsTest, IsTunnelingSafeReturnsTrueWhenDisplacementFits) {
    // displacement = 0.5 * 0.016 = 0.008; colliders sum = 0.5 → safe
    EXPECT_TRUE(IRMath::isTunnelingSafe(0.5f, 0.016f, 0.25f, 0.25f));
}

TEST(PhysicsTest, IsTunnelingSafeReturnsFalseWhenDisplacementExceedsColliders) {
    // displacement = 100 * 0.016 = 1.6; colliders sum = 0.5 → unsafe
    EXPECT_FALSE(IRMath::isTunnelingSafe(100.0f, 0.016f, 0.25f, 0.25f));
}

TEST(PhysicsTest, IsTunnelingSafeBoundaryIsStrictlyLessThan) {
    // displacement == collider sum → NOT safe (strict <)
    float velocity = 10.0f;
    float dt = 0.1f;
    float thicknessA = 0.5f;
    float thicknessB = 0.5f;  // sum = 1.0, equals velocity * dt
    EXPECT_FALSE(IRMath::isTunnelingSafe(velocity, dt, thicknessA, thicknessB));
}

TEST(PhysicsTest, IsTunnelingSafeZeroVelocityIsAlwaysSafe) {
    EXPECT_TRUE(IRMath::isTunnelingSafe(0.0f, 0.016f, 0.01f, 0.01f));
}

TEST(PhysicsTest, IsTunnelingSafeLargeCollidersSafe) {
    // Very thick colliders dominate even a fast object
    EXPECT_TRUE(IRMath::isTunnelingSafe(1000.0f, 0.016f, 500.0f, 500.0f));
}

} // namespace
