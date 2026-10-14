#include <gtest/gtest.h>

#include <irreden/render/ir_render_types.hpp>

namespace {

// The default-pivot latch reads raster row `H / 2`, the row
// `RenderManager::getCanvasCenterIso` describes, on every backend and height.
void expectCenterPixelReadsRasterCenter(IRMath::ivec2 resolution) {
    const IRMath::ivec2 center = IRRender::framebufferCenterPixel(resolution);
    EXPECT_EQ(center.x, resolution.x / 2);
    EXPECT_EQ(IRRender::compositeDepthTextureRow(center.y, resolution.y), resolution.y / 2);
}

} // namespace

TEST(CompositeDepthRow, CenterPixelReadsRasterCenterAtEvenHeight) {
    expectCenterPixelReadsRasterCenter(IRMath::ivec2(1284, 722));
}

TEST(CompositeDepthRow, CenterPixelReadsRasterCenterAtOddHeight) {
    expectCenterPixelReadsRasterCenter(IRMath::ivec2(645, 363));
}

TEST(CompositeDepthRow, EvenHeightCenterIsTheUpperOfTheTwoMiddleRows) {
    EXPECT_EQ(IRRender::framebufferCenterPixel(IRMath::ivec2(1284, 722)).y, 360);
}

TEST(CompositeDepthRow, TopLeftRowsMapToMirroredTextureRows) {
    EXPECT_EQ(IRRender::compositeDepthTextureRow(0, 722), 721);
    EXPECT_EQ(IRRender::compositeDepthTextureRow(721, 722), 0);
}
