#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>
#include <irreden/render/camera.hpp>

namespace {

constexpr float kTolerance = 1e-5f;

using IRPrefab::Camera::computeYawSplit;

// ---------------------------------------------------------------------------
// IRPrefab::Camera::detail wrap/extract behavior — exercised via the public
// yawFromQuat and wrapYaw helpers. The wrapping normalizes any input yaw to
// the half-open range [-π, π).
// ---------------------------------------------------------------------------

TEST(CameraYawWrap, ZeroStaysZero) {
    float yaw = IRPrefab::Camera::detail::wrapYaw(0.0f);
    EXPECT_NEAR(yaw, 0.0f, kTolerance);
}

TEST(CameraYawWrap, PositivePiMapsToNegativePi) {
    float yaw = IRPrefab::Camera::detail::wrapYaw(IRMath::kPi);
    EXPECT_NEAR(yaw, -IRMath::kPi, kTolerance);
}

TEST(CameraYawWrap, NegativePiStaysNegativePi) {
    float yaw = IRPrefab::Camera::detail::wrapYaw(-IRMath::kPi);
    EXPECT_NEAR(yaw, -IRMath::kPi, kTolerance);
}

TEST(CameraYawWrap, FullRotationWrapsToZero) {
    float yaw = IRPrefab::Camera::detail::wrapYaw(IRMath::kTwoPi);
    EXPECT_NEAR(yaw, 0.0f, kTolerance);
}

TEST(CameraYawWrap, NegativeFullRotationWrapsToZero) {
    float yaw = IRPrefab::Camera::detail::wrapYaw(-IRMath::kTwoPi);
    EXPECT_NEAR(yaw, 0.0f, kTolerance);
}

TEST(CameraYawWrap, ManyFullRotationsDoNotDriftMaterially) {
    float yaw = 0.5f;
    for (int i = 0; i < 1000; ++i) {
        yaw += IRMath::kTwoPi;
        yaw = IRPrefab::Camera::detail::wrapYaw(yaw);
    }
    EXPECT_NEAR(yaw, 0.5f, 1e-3f);
}

// ---------------------------------------------------------------------------
// Quaternion round-trip: setYaw → getYaw via quatAxisAngle(z, angle).
// Verifies yawFromQuat extracts the Z-component correctly.
// ---------------------------------------------------------------------------

TEST(CameraYawQuat, ZeroRoundTrips) {
    auto q = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), 0.0f);
    EXPECT_NEAR(IRPrefab::Camera::detail::yawFromQuat(q), 0.0f, kTolerance);
}

TEST(CameraYawQuat, HalfPiRoundTrips) {
    constexpr float halfPi = IRMath::kHalfPi;
    auto q = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), halfPi);
    EXPECT_NEAR(IRPrefab::Camera::detail::yawFromQuat(q), halfPi, kTolerance);
}

TEST(CameraYawQuat, NegativeHalfPiRoundTrips) {
    constexpr float halfPi = IRMath::kHalfPi;
    auto q = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), -halfPi);
    EXPECT_NEAR(IRPrefab::Camera::detail::yawFromQuat(q), -halfPi, kTolerance);
}

TEST(CameraYawQuat, NearPiRoundTrips) {
    constexpr float nearPi = IRMath::kPi - 0.01f;
    auto q = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), nearPi);
    EXPECT_NEAR(IRPrefab::Camera::detail::yawFromQuat(q), nearPi, kTolerance);
}

TEST(CameraYawQuat, NegativeNearPiRoundTrips) {
    constexpr float nearPi = -(IRMath::kPi - 0.01f);
    auto q = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), nearPi);
    EXPECT_NEAR(IRPrefab::Camera::detail::yawFromQuat(q), nearPi, kTolerance);
}

TEST(CameraYawQuat, IdentityQuatGivesZeroYaw) {
    auto q = IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(IRPrefab::Camera::detail::yawFromQuat(q), 0.0f, kTolerance);
}

TEST(CameraYawQuat, IteratedRoundTripDoesNotDriftMaterially) {
    // Exercises the full yawFromQuat → wrapYaw → quatAxisAngle round-trip 1000
    // times — the AUTO_YAW_ROTATE use case that rotateYaw() drives each frame.
    auto q = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), 0.5f);
    const float initial = IRPrefab::Camera::detail::yawFromQuat(q);
    for (int i = 0; i < 1000; ++i) {
        float yaw = IRPrefab::Camera::detail::yawFromQuat(q);
        yaw = IRPrefab::Camera::detail::wrapYaw(yaw + IRMath::kTwoPi);
        q = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), yaw);
    }
    EXPECT_NEAR(IRPrefab::Camera::detail::yawFromQuat(q), initial, 1e-3f);
}

// ---------------------------------------------------------------------------
// IRMath::quatExtractZAngle round-trip with quatAxisAngle(z, y)
//
// The backward-compat surface `IRPrefab::Camera::setYaw(y) → getYaw() == y`
// rests on this round-trip identity, since `setYaw` writes
// `quatAxisAngle(z, y)` and `getYaw` extracts via `quatExtractZAngle`.
// The wrap range is (-π, π] — yaws outside that interval wrap on read.
// ---------------------------------------------------------------------------

float roundTripZ(float yaw) {
    return IRMath::quatExtractZAngle(IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), yaw));
}

TEST(QuatExtractZAngle, ZeroRoundTripsExactly) {
    EXPECT_NEAR(roundTripZ(0.0f), 0.0f, kTolerance);
}

TEST(QuatExtractZAngle, HalfPiRoundTripsExactly) {
    EXPECT_NEAR(roundTripZ(IRMath::kHalfPi), IRMath::kHalfPi, kTolerance);
}

TEST(QuatExtractZAngle, NegativeHalfPiRoundTripsExactly) {
    EXPECT_NEAR(roundTripZ(-IRMath::kHalfPi), -IRMath::kHalfPi, kTolerance);
}

TEST(QuatExtractZAngle, QuarterPiRoundTripsExactly) {
    EXPECT_NEAR(roundTripZ(IRMath::kQuarterPi), IRMath::kQuarterPi, kTolerance);
}

TEST(QuatExtractZAngle, ArbitraryAnglesInsideRangeRoundTrip) {
    // Sweep across the open interval (-π, π) — the ±π endpoints sit on
    // the atan2 branch cut where round-trip flips sign at float precision.
    for (int i = -99; i <= 99; ++i) {
        const float yaw = (static_cast<float>(i) / 100.0f) * IRMath::kPi;
        EXPECT_NEAR(roundTripZ(yaw), yaw, kTolerance) << "yaw=" << yaw;
    }
}

TEST(QuatExtractZAngle, PiBoundaryWrapsToSymmetricNegative) {
    // ±π represent the same rotation; atan2's branch cut means a yaw at
    // the boundary may round-trip to either sign at fp precision. Either
    // is fine for downstream consumers (both project to the same basis).
    const float result = roundTripZ(IRMath::kPi);
    EXPECT_TRUE(
        IRMath::abs(result - IRMath::kPi) < 1e-4f || IRMath::abs(result + IRMath::kPi) < 1e-4f
    ) << "result="
      << result;
}

TEST(QuatExtractZAngle, IdentityQuatExtractsZero) {
    EXPECT_NEAR(IRMath::quatExtractZAngle(IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f)), 0.0f, kTolerance);
}

TEST(QuatExtractZAngle, PurePitchExtractsZeroYaw) {
    // A pure-X rotation has no Z-component; extracted yaw is 0.
    const IRMath::vec4 pitchQuat =
        IRMath::quatAxisAngle(IRMath::vec3(1.0f, 0.0f, 0.0f), IRMath::kPi / 3.0f);
    EXPECT_NEAR(IRMath::quatExtractZAngle(pitchQuat), 0.0f, kTolerance);
}

// ---------------------------------------------------------------------------
// IRPrefab::Camera::computeYawSplit
// rasterYaw   = round(visualYaw / (π/2)) * (π/2)
// residualYaw = visualYaw - rasterYaw, lies in [-π/4, π/4]
// ---------------------------------------------------------------------------

TEST(YawSplit, ZeroSplitsToZeroZero) {
    auto [raster, residual] = computeYawSplit(0.0f);
    EXPECT_NEAR(raster, 0.0f, kTolerance);
    EXPECT_NEAR(residual, 0.0f, kTolerance);
}

TEST(YawSplit, ExactCardinalsHaveZeroResidual) {
    constexpr float halfPi = IRMath::kHalfPi;
    for (float k : {-2.0f, -1.0f, 0.0f, 1.0f, 2.0f}) {
        auto [raster, residual] = computeYawSplit(k * halfPi);
        EXPECT_NEAR(raster, k * halfPi, kTolerance) << "k=" << k;
        EXPECT_NEAR(residual, 0.0f, kTolerance) << "k=" << k;
    }
}

TEST(YawSplit, PositiveQuarterPiSnapsAwayFromZero) {
    constexpr float halfPi = IRMath::kHalfPi;
    constexpr float quarterPi = IRMath::kQuarterPi;
    auto [raster, residual] = computeYawSplit(quarterPi);
    EXPECT_NEAR(raster, halfPi, kTolerance);
    EXPECT_NEAR(residual, -quarterPi, kTolerance);
}

TEST(YawSplit, NegativeQuarterPiSnapsAwayFromZero) {
    constexpr float halfPi = IRMath::kHalfPi;
    constexpr float quarterPi = IRMath::kQuarterPi;
    auto [raster, residual] = computeYawSplit(-quarterPi);
    EXPECT_NEAR(raster, -halfPi, kTolerance);
    EXPECT_NEAR(residual, quarterPi, kTolerance);
}

TEST(YawSplit, ResidualStaysInsideCardinalQuarter) {
    constexpr float quarterPi = IRMath::kQuarterPi;
    for (int i = -100; i <= 100; ++i) {
        const float visualYaw = (static_cast<float>(i) / 100.0f) * IRMath::kPi;
        auto [raster, residual] = computeYawSplit(visualYaw);
        EXPECT_LE(residual, quarterPi + kTolerance) << "visualYaw=" << visualYaw;
        EXPECT_GE(residual, -quarterPi - kTolerance) << "visualYaw=" << visualYaw;
        EXPECT_NEAR(raster + residual, visualYaw, kTolerance) << "visualYaw=" << visualYaw;
    }
}

} // namespace
