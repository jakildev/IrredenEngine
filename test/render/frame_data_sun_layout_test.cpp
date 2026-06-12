#include <gtest/gtest.h>

#include <irreden/render/ir_render_types.hpp>

#include <cstddef>

// Runtime tripwire mirroring the compile-time static_asserts in ir_render_types.hpp (std140 leading edge).
TEST(FrameDataSunLayout, MatchesStd140Packing) {
    EXPECT_EQ(sizeof(IRRender::FrameDataSun), 128u);
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
}
