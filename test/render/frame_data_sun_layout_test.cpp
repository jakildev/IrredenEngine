#include <gtest/gtest.h>

#include <irreden/render/ir_render_types.hpp>

#include <cstddef>

TEST(FrameDataSunLayout, MatchesStd140Packing) {
    EXPECT_EQ(sizeof(IRRender::FrameDataSun), 48u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunDirection_), 0u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunIntensity_), 16u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunAmbient_), 20u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, shadowsEnabled_), 24u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, shapeCasterCount_), 28u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, occupancyBoundsCount_), 32u);
}

TEST(GPUOccupancyEntityBoundsLayout, MatchesStd430Packing) {
    EXPECT_EQ(sizeof(IRRender::GPUOccupancyEntityBounds), 48u);
    EXPECT_EQ(offsetof(IRRender::GPUOccupancyEntityBounds, entityId), 0u);
    EXPECT_EQ(offsetof(IRRender::GPUOccupancyEntityBounds, minCell), 16u);
    EXPECT_EQ(offsetof(IRRender::GPUOccupancyEntityBounds, maxCell), 32u);
}
