#include <gtest/gtest.h>

#include <irreden/render/camera.hpp>

#include <glm/gtc/constants.hpp>

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
    float yaw = IRPrefab::Camera::detail::wrapYaw(glm::pi<float>());
    EXPECT_NEAR(yaw, -glm::pi<float>(), kTolerance);
}

TEST(CameraYawWrap, NegativePiStaysNegativePi) {
    float yaw = IRPrefab::Camera::detail::wrapYaw(-glm::pi<float>());
    EXPECT_NEAR(yaw, -glm::pi<float>(), kTolerance);
}

TEST(CameraYawWrap, FullRotationWrapsToZero) {
    float yaw = IRPrefab::Camera::detail::wrapYaw(glm::two_pi<float>());
    EXPECT_NEAR(yaw, 0.0f, kTolerance);
}

TEST(CameraYawWrap, NegativeFullRotationWrapsToZero) {
    float yaw = IRPrefab::Camera::detail::wrapYaw(-glm::two_pi<float>());
    EXPECT_NEAR(yaw, 0.0f, kTolerance);
}

TEST(CameraYawWrap, ManyFullRotationsDoNotDriftMaterially) {
    float yaw = 0.5f;
    for (int i = 0; i < 1000; ++i) {
        yaw += glm::two_pi<float>();
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
    constexpr float halfPi = glm::half_pi<float>();
    auto q = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), halfPi);
    EXPECT_NEAR(IRPrefab::Camera::detail::yawFromQuat(q), halfPi, kTolerance);
}

TEST(CameraYawQuat, NegativeHalfPiRoundTrips) {
    constexpr float halfPi = glm::half_pi<float>();
    auto q = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), -halfPi);
    EXPECT_NEAR(IRPrefab::Camera::detail::yawFromQuat(q), -halfPi, kTolerance);
}

TEST(CameraYawQuat, NearPiRoundTrips) {
    constexpr float nearPi = glm::pi<float>() - 0.01f;
    auto q = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), nearPi);
    EXPECT_NEAR(IRPrefab::Camera::detail::yawFromQuat(q), nearPi, kTolerance);
}

TEST(CameraYawQuat, NegativeNearPiRoundTrips) {
    constexpr float nearPi = -(glm::pi<float>() - 0.01f);
    auto q = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), nearPi);
    EXPECT_NEAR(IRPrefab::Camera::detail::yawFromQuat(q), nearPi, kTolerance);
}

TEST(CameraYawQuat, IdentityQuatGivesZeroYaw) {
    auto q = IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(IRPrefab::Camera::detail::yawFromQuat(q), 0.0f, kTolerance);
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
    constexpr float halfPi = glm::half_pi<float>();
    for (float k : {-2.0f, -1.0f, 0.0f, 1.0f, 2.0f}) {
        auto [raster, residual] = computeYawSplit(k * halfPi);
        EXPECT_NEAR(raster, k * halfPi, kTolerance) << "k=" << k;
        EXPECT_NEAR(residual, 0.0f, kTolerance) << "k=" << k;
    }
}

TEST(YawSplit, PositiveQuarterPiSnapsAwayFromZero) {
    constexpr float halfPi = glm::half_pi<float>();
    constexpr float quarterPi = glm::quarter_pi<float>();
    auto [raster, residual] = computeYawSplit(quarterPi);
    EXPECT_NEAR(raster, halfPi, kTolerance);
    EXPECT_NEAR(residual, -quarterPi, kTolerance);
}

TEST(YawSplit, NegativeQuarterPiSnapsAwayFromZero) {
    constexpr float halfPi = glm::half_pi<float>();
    constexpr float quarterPi = glm::quarter_pi<float>();
    auto [raster, residual] = computeYawSplit(-quarterPi);
    EXPECT_NEAR(raster, -halfPi, kTolerance);
    EXPECT_NEAR(residual, quarterPi, kTolerance);
}

TEST(YawSplit, ResidualStaysInsideCardinalQuarter) {
    constexpr float quarterPi = glm::quarter_pi<float>();
    for (int i = -100; i <= 100; ++i) {
        const float visualYaw = (static_cast<float>(i) / 100.0f) * glm::pi<float>();
        auto [raster, residual] = computeYawSplit(visualYaw);
        EXPECT_LE(residual, quarterPi + kTolerance) << "visualYaw=" << visualYaw;
        EXPECT_GE(residual, -quarterPi - kTolerance) << "visualYaw=" << visualYaw;
        EXPECT_NEAR(raster + residual, visualYaw, kTolerance) << "visualYaw=" << visualYaw;
    }
}

} // namespace
