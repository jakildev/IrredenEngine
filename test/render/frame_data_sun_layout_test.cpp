#include <gtest/gtest.h>

#include <irreden/render/ir_render_types.hpp>

#include <cstddef>

// Runtime tripwire mirroring the compile-time static_asserts in ir_render_types.hpp (std140 leading edge).
TEST(FrameDataSunLayout, MatchesStd140Packing) {
    EXPECT_EQ(sizeof(IRRender::FrameDataSun), 160u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunDirection_), 0u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunIntensity_), 16u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunAmbient_), 20u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, shadowsEnabled_), 24u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, aoEnabled_), 28u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunBasisU_), 32u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunBasisV_), 48u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunBufferOriginUV_), 64u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunBufferTexelSize_), 72u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, cascadeOriginUV_0_), 80u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, cascadeTexelSize_0_), 88u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, cascadeOriginUV_1_), 96u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, cascadeTexelSize_1_), 104u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, cascadeSplitDepth_), 112u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, cascadeCount_), 116u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunSplatMaxTexels_), 120u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunMaxShadowThrow_), 124u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunCasterViewToWorld_), 128u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, fogCeilingEnabled_), 144u);
}

TEST(FrameDataSunLayout, DefaultCasterBasisIsIdentity) {
    const IRRender::FrameDataSun frame;
    EXPECT_EQ(frame.sunCasterViewToWorld_, IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(frame.fogCeilingEnabled_, IRMath::ivec4(0));
}
