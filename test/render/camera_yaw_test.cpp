#include <gtest/gtest.h>

#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_camera_yaw.hpp>

#include <glm/gtc/constants.hpp>

namespace {

constexpr float kTolerance = 1e-5f;

using IRComponents::C_CameraYaw;
using IRPrefab::Camera::computeYawSplit;

// ---------------------------------------------------------------------------
// C_CameraYaw wrap behavior — exercised via the public mutation surface
// (constructor, setVisualYaw, rotate). The private wrap() must normalize any
// input yaw to the half-open range [-π, π).
// ---------------------------------------------------------------------------

TEST(CameraYawWrap, ZeroStaysZero) {
    C_CameraYaw c{0.0f};
    EXPECT_NEAR(c.visualYaw_, 0.0f, kTolerance);
}

TEST(CameraYawWrap, PositivePiMapsToNegativePi) {
    // π is the exclusive upper bound of [-π, π) so it must wrap to -π.
    C_CameraYaw c{glm::pi<float>()};
    EXPECT_NEAR(c.visualYaw_, -glm::pi<float>(), kTolerance);
}

TEST(CameraYawWrap, NegativePiStaysNegativePi) {
    // -π is the inclusive lower bound — must remain -π, not flip to +π.
    C_CameraYaw c{-glm::pi<float>()};
    EXPECT_NEAR(c.visualYaw_, -glm::pi<float>(), kTolerance);
}

TEST(CameraYawWrap, FullRotationWrapsToZero) {
    C_CameraYaw c{glm::two_pi<float>()};
    EXPECT_NEAR(c.visualYaw_, 0.0f, kTolerance);
}

TEST(CameraYawWrap, NegativeFullRotationWrapsToZero) {
    C_CameraYaw c{-glm::two_pi<float>()};
    EXPECT_NEAR(c.visualYaw_, 0.0f, kTolerance);
}

TEST(CameraYawWrap, SetVisualYawWraps) {
    C_CameraYaw c{};
    c.setVisualYaw(glm::pi<float>());
    EXPECT_NEAR(c.visualYaw_, -glm::pi<float>(), kTolerance);
}

TEST(CameraYawWrap, RotatePastPositivePiWrapsToNegativeSide) {
    // Step from just below +π through the boundary — the result must land
    // on the symmetric negative side, not pop above +π.
    C_CameraYaw c{glm::pi<float>() - 0.1f};
    c.rotate(0.2f);
    EXPECT_NEAR(c.visualYaw_, -glm::pi<float>() + 0.1f, kTolerance);
}

TEST(CameraYawWrap, ManyFullRotationsDoNotDriftMaterially) {
    // Long-run sanity: the whole point of wrapping on every mutation is to
    // prevent float drift. 1000 full rotations from 0.5 must come back near
    // 0.5 within a coarse tolerance (1000×two_pi accumulates some noise).
    C_CameraYaw c{0.5f};
    for (int i = 0; i < 1000; ++i) c.rotate(glm::two_pi<float>());
    EXPECT_NEAR(c.visualYaw_, 0.5f, 1e-3f);
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
    // π/4 = 0.5 cardinals away from 0. C++ std::round rounds halves away
    // from zero, so this snaps up to π/2 with residual -π/4 (boundary).
    constexpr float halfPi = glm::half_pi<float>();
    constexpr float quarterPi = glm::quarter_pi<float>();
    auto [raster, residual] = computeYawSplit(quarterPi);
    EXPECT_NEAR(raster, halfPi, kTolerance);
    EXPECT_NEAR(residual, -quarterPi, kTolerance);
}

TEST(YawSplit, NegativeQuarterPiSnapsAwayFromZero) {
    // Symmetric: -π/4 snaps down to -π/2 with residual +π/4.
    constexpr float halfPi = glm::half_pi<float>();
    constexpr float quarterPi = glm::quarter_pi<float>();
    auto [raster, residual] = computeYawSplit(-quarterPi);
    EXPECT_NEAR(raster, -halfPi, kTolerance);
    EXPECT_NEAR(residual, quarterPi, kTolerance);
}

TEST(YawSplit, ResidualStaysInsideCardinalQuarter) {
    // Sweep [-π, π] and confirm the residual is always within [-π/4, π/4]
    // and that raster + residual reconstructs the input. The screen-space
    // residual composite pass relies on this invariant for canvas bounds.
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
