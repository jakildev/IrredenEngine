// Sun-lighting layout / config tests.
//
// These tests cover the parts of the new sun lighting pipeline that are
// reachable without a fully-initialized RenderManager: the GPU-facing
// `FrameDataSun` struct (size, defaults) and the `C_LightSource`
// component used to drive a directional override.
//
// The original audit asked for a round-trip test of
// `IRRender::setSunIntensity` / `setSunAmbient` / `setSunShadowsEnabled`
// through their getters. That round-trip cannot run here because those
// free functions forward to `RenderManager`, which IR_ASSERTs on
// `g_renderManager == nullptr` until the engine is initialized — and the
// unit-test binary doesn't construct a RenderManager. The negative path
// for `setSunDirection` (rejecting +Z input) IS testable via
// `IR_ASSERT`, since that runs *before* the RenderManager forward; it
// lives in `sun_direction_test.cpp`.

#include <gtest/gtest.h>

#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/ir_render_types.hpp>

using namespace IRComponents;

TEST(SunLightingConfig, FrameDataDefaultsKeepShadowsEnabled) {
    IRRender::FrameDataSun frameData{};

    EXPECT_FLOAT_EQ(frameData.sunIntensity_, 1.0f);
    EXPECT_FLOAT_EQ(frameData.sunAmbient_, 0.4f);
    EXPECT_EQ(frameData.shadowsEnabled_, 1);
    EXPECT_EQ(frameData.shapeCasterCount_, 0);
}

TEST(SunLightingConfig, FrameDataDefaultDirectionMatchesRenderManager) {
    // Mirror of RenderManager::m_sunDirection (overhead with small +X /
    // +Y tilt). Both initializers must stay in lockstep — the GPU UBO
    // is overwritten from RenderManager state on the first tick, but the
    // default matters for any path that reads the UBO before a tick
    // runs (e.g. headless tools, tests).
    IRRender::FrameDataSun frameData{};
    EXPECT_FLOAT_EQ(frameData.sunDirection_.x, 0.3f);
    EXPECT_FLOAT_EQ(frameData.sunDirection_.y, 0.2f);
    EXPECT_FLOAT_EQ(frameData.sunDirection_.z, -0.93f);
    EXPECT_LE(frameData.sunDirection_.z, 0.0f); // +Z is down — sun must be above
}

TEST(SunLightingConfig, DirectionalLightCarriesAmbientFloor) {
    C_LightSource light{
        LightType::DIRECTIONAL,
        IRMath::IRColors::kWhite,
        1.25f,
        static_cast<std::uint8_t>(0),
        IRMath::vec3(0.35f, 0.85f, -0.4f),
        45.0f,
        0.35f
    };

    EXPECT_EQ(light.type_, LightType::DIRECTIONAL);
    EXPECT_FLOAT_EQ(light.intensity_, 1.25f);
    EXPECT_FLOAT_EQ(light.ambient_, 0.35f);
}

TEST(SunLightingConfig, DefaultLightSourceUsesSpecAmbientFloor) {
    // Default-constructed C_LightSource picks up the engine's default
    // ambient floor (0.4). Demos rely on this so a creation that builds
    // a directional light without explicitly setting ambient still
    // matches the render manager's default sun ambient.
    C_LightSource light{};
    EXPECT_FLOAT_EQ(light.ambient_, 0.4f);
}
