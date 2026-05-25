#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>
#include <irreden/render/camera.hpp>

namespace {

constexpr float kTolerance = 1e-5f;

using IRPrefab::Camera::computeYawSplit;

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
    EXPECT_NEAR(roundTripZ(IRMath::kPi / 4.0f), IRMath::kPi / 4.0f, kTolerance);
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
    // Identity quaternion (0,0,0,1) means no rotation — extracted yaw is 0.
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
    for (float k : {-2.0f, -1.0f, 0.0f, 1.0f, 2.0f}) {
        auto [raster, residual] = computeYawSplit(k * IRMath::kHalfPi);
        EXPECT_NEAR(raster, k * IRMath::kHalfPi, kTolerance) << "k=" << k;
        EXPECT_NEAR(residual, 0.0f, kTolerance) << "k=" << k;
    }
}

TEST(YawSplit, PositiveQuarterPiSnapsAwayFromZero) {
    // π/4 = 0.5 cardinals away from 0. glm::round rounds halves away
    // from zero, so this snaps up to π/2 with residual -π/4 (boundary).
    const float quarterPi = IRMath::kPi / 4.0f;
    auto [raster, residual] = computeYawSplit(quarterPi);
    EXPECT_NEAR(raster, IRMath::kHalfPi, kTolerance);
    EXPECT_NEAR(residual, -quarterPi, kTolerance);
}

TEST(YawSplit, NegativeQuarterPiSnapsAwayFromZero) {
    // Symmetric: -π/4 snaps down to -π/2 with residual +π/4.
    const float quarterPi = IRMath::kPi / 4.0f;
    auto [raster, residual] = computeYawSplit(-quarterPi);
    EXPECT_NEAR(raster, -IRMath::kHalfPi, kTolerance);
    EXPECT_NEAR(residual, quarterPi, kTolerance);
}

TEST(YawSplit, ResidualStaysInsideCardinalQuarter) {
    // Sweep [-π, π] and confirm the residual is always within [-π/4, π/4]
    // and that raster + residual reconstructs the input. The screen-space
    // residual composite pass relies on this invariant for canvas bounds.
    const float quarterPi = IRMath::kPi / 4.0f;
    for (int i = -100; i <= 100; ++i) {
        const float visualYaw = (static_cast<float>(i) / 100.0f) * IRMath::kPi;
        auto [raster, residual] = computeYawSplit(visualYaw);
        EXPECT_LE(residual, quarterPi + kTolerance) << "visualYaw=" << visualYaw;
        EXPECT_GE(residual, -quarterPi - kTolerance) << "visualYaw=" << visualYaw;
        EXPECT_NEAR(raster + residual, visualYaw, kTolerance) << "visualYaw=" << visualYaw;
    }
}

} // namespace
