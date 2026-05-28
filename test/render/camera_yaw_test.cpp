#include <gtest/gtest.h>

#include <irreden/render/camera.hpp>
#include <irreden/ir_math.hpp>

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

TEST(CameraYawWrap, PositivePiClampsToEpsilonAboveNegativePi) {
    // Both +π and -π map to -π before the ε guard, which then bumps to
    // -π + 1e-4 so pitchFromQuat's atan2(q.x, q.w) never sees cos(yaw/2)=0.
    float yaw = IRPrefab::Camera::detail::wrapYaw(IRMath::kPi);
    EXPECT_NEAR(yaw, -IRMath::kPi + 1e-4f, kTolerance);
}

TEST(CameraYawWrap, NegativePiClampsToEpsilonAbove) {
    // -π is the wrap boundary where cos(yaw/2) = 0; the ε guard nudges the
    // result to -π + 1e-4 so pitchFromQuat remains valid.
    float yaw = IRPrefab::Camera::detail::wrapYaw(-IRMath::kPi);
    EXPECT_NEAR(yaw, -IRMath::kPi + 1e-4f, kTolerance);
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
// Pitch preservation at the wrapYaw boundary.
// Verifies the ε guard in wrapYaw keeps pitchFromQuat valid at yaw = -π.
// ---------------------------------------------------------------------------

TEST(CameraYawPitchPreservation, PitchPreservedAtNegativePiBoundary) {
    // wrapYaw(-π) returns -π + 1e-4 instead of -π, so cos(yaw/2) ≠ 0 and
    // pitchFromQuat can recover the pitch without reading atan2(0, 0) = 0.
    const float pitch = 0.3f;
    const float wrappedYaw = IRPrefab::Camera::detail::wrapYaw(-IRMath::kPi);
    const auto q = IRPrefab::Camera::detail::quatFromYawPitch(wrappedYaw, pitch);
    EXPECT_NEAR(IRPrefab::Camera::detail::pitchFromQuat(q), pitch, kTolerance);
}

TEST(CameraYawPitchPreservation, PitchPreservedAfterYawRotateThroughBoundary) {
    // Simulates rotateYaw(δ) when the camera sits near yaw = -π.
    // The pitch must survive even as wrapYaw clamps the result.
    const float pitch = 0.5f;
    float yaw = -IRMath::kPi - 0.001f;  // just past the boundary
    yaw = IRPrefab::Camera::detail::wrapYaw(yaw);
    const auto q = IRPrefab::Camera::detail::quatFromYawPitch(yaw, pitch);
    EXPECT_NEAR(IRPrefab::Camera::detail::pitchFromQuat(q), pitch, kTolerance);
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

// ---------------------------------------------------------------------------
// IRMath::quatExtractZAngle — ZYX Tait-Bryan yaw extraction
//
// For pure-Z quaternions (the only case the camera currently exercises)
// this must round-trip exactly. The decomposition formula is also correct
// for general SO(3) — verified for pure-pitch (extracts zero yaw) and for
// the ±π branch-cut boundary.
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
    EXPECT_NEAR(roundTripZ(IRMath::kPi / 4.0f), IRMath::kPi / 4.0f, kTolerance);
}

TEST(QuatExtractZAngle, ArbitraryAnglesInsideRangeRoundTrip) {
    // Sweep across (-π, π) — ±π sit on the atan2 branch cut where fp
    // rounding may flip sign. All other values must round-trip exactly.
    for (int i = -99; i <= 99; ++i) {
        const float yaw = (static_cast<float>(i) / 100.0f) * IRMath::kPi;
        EXPECT_NEAR(roundTripZ(yaw), yaw, kTolerance) << "yaw=" << yaw;
    }
}

TEST(QuatExtractZAngle, PiBoundaryWrapsToSymmetricNegative) {
    // ±π represent the same rotation; atan2's branch cut means the result
    // at exactly π may be either ±π at fp precision — both are correct.
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
    // A pure-X rotation has no Z-component; extracted yaw must be 0.
    const IRMath::vec4 pitchQuat =
        IRMath::quatAxisAngle(IRMath::vec3(1.0f, 0.0f, 0.0f), IRMath::kPi / 3.0f);
    EXPECT_NEAR(IRMath::quatExtractZAngle(pitchQuat), 0.0f, kTolerance);
}

} // namespace
