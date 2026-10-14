#include <gtest/gtest.h>

#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/systems/system_sprites_to_screen.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/utility/file_utils.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace {

using IRRender::ResourceId;
using IRRender::SpriteRenderEntry;

// Friendly alias for the system specialization under test.
using SpritesToScreenSystem = IRSystem::System<IRSystem::SPRITE_TO_SCREEN>;

SpriteRenderEntry makeEntry(ResourceId tex, int isoDepth) {
    SpriteRenderEntry e{};
    e.textureHandle_ = tex;
    e.isoDepth_ = isoDepth;
    return e;
}

std::string readShaderSource(const std::string &relativePath) {
    const std::string path = std::string(IR_TEST_RENDER_SHADER_DIR) + "/" + relativePath;
    return IRUtility::readFileAsString(path);
}

TEST(SpritesToScreenBinding, FrameDataAvoidsMetalVertexStreamSlot) {
    EXPECT_EQ(IRRender::kBufferIndex_SpritesFrameData, 27u);
    EXPECT_EQ(
        IRRender::kBufferIndex_SpritesFrameData,
        IRRender::kBufferIndex_FrameDataLightingToTrixel
    );
    EXPECT_NE(IRRender::kBufferIndex_SpritesFrameData, 0u);

    const std::string glsl = readShaderSource("v_sprites_to_screen.glsl");
    const std::string metal = readShaderSource("metal/sprites_to_screen.metal");
    EXPECT_NE(glsl.find("layout (std140, binding = 27) uniform FrameData"), std::string::npos);
    EXPECT_NE(
        metal.find("constant FrameDataSpritesToScreen &frame [[buffer(27)]]"),
        std::string::npos
    );
}

// CPU sort rule: sprites group by texture (so each texture's run packs into
// one drawArraysInstanced call) and within a texture sort back-to-front by
// iso depth (largest depth first; nearer sprites land on top).
TEST(SpritesToScreenSort, GroupsByTextureThenDepthDescending) {
    std::vector<SpriteRenderEntry> entries{
        makeEntry(/*tex=*/2, /*depth=*/5),
        makeEntry(1, 10),
        makeEntry(2, 30),
        makeEntry(1, 20),
        makeEntry(2, 10),
        makeEntry(1, 30),
    };

    std::sort(entries.begin(), entries.end(), &SpritesToScreenSystem::entryLess);

    ASSERT_EQ(entries.size(), 6u);
    EXPECT_EQ(entries[0].textureHandle_, 1u);
    EXPECT_EQ(entries[0].isoDepth_, 30);
    EXPECT_EQ(entries[1].textureHandle_, 1u);
    EXPECT_EQ(entries[1].isoDepth_, 20);
    EXPECT_EQ(entries[2].textureHandle_, 1u);
    EXPECT_EQ(entries[2].isoDepth_, 10);
    EXPECT_EQ(entries[3].textureHandle_, 2u);
    EXPECT_EQ(entries[3].isoDepth_, 30);
    EXPECT_EQ(entries[4].textureHandle_, 2u);
    EXPECT_EQ(entries[4].isoDepth_, 10);
    EXPECT_EQ(entries[5].textureHandle_, 2u);
    EXPECT_EQ(entries[5].isoDepth_, 5);
}

// Acceptance criterion 3 (single-atlas case): 50+ sprites all sharing one
// texture handle resolve to ONE Group. The system then issues exactly one
// drawArraysInstanced call against that group.
TEST(SpritesToScreenSort, SingleAtlasMakesOneGroup) {
    std::vector<SpriteRenderEntry> entries;
    entries.reserve(64);
    for (int i = 0; i < 64; ++i) {
        entries.push_back(makeEntry(7, i * 2));
    }
    std::sort(entries.begin(), entries.end(), &SpritesToScreenSystem::entryLess);
    auto groups = SpritesToScreenSystem::buildGroups(entries);

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].textureHandle_, 7u);
    EXPECT_EQ(groups[0].firstEntry_, 0u);
    EXPECT_EQ(groups[0].count_, 64u);
}

// Multi-atlas: each contiguous run of equal handles forms one Group, and
// the firstEntry_ offsets map back into the sorted entry vector with no
// gaps so the per-group SSBO upload stays contiguous.
TEST(SpritesToScreenSort, MultiAtlasMakesOneGroupPerHandle) {
    std::vector<SpriteRenderEntry> entries{
        makeEntry(1, 30),
        makeEntry(1, 20),
        makeEntry(1, 10),
        makeEntry(2, 30),
        makeEntry(2, 5),
        makeEntry(3, 100),
    };
    auto groups = SpritesToScreenSystem::buildGroups(entries);

    ASSERT_EQ(groups.size(), 3u);
    EXPECT_EQ(groups[0].textureHandle_, 1u);
    EXPECT_EQ(groups[0].firstEntry_, 0u);
    EXPECT_EQ(groups[0].count_, 3u);
    EXPECT_EQ(groups[1].textureHandle_, 2u);
    EXPECT_EQ(groups[1].firstEntry_, 3u);
    EXPECT_EQ(groups[1].count_, 2u);
    EXPECT_EQ(groups[2].textureHandle_, 3u);
    EXPECT_EQ(groups[2].firstEntry_, 5u);
    EXPECT_EQ(groups[2].count_, 1u);
}

// Empty entry vector: buildGroups returns no groups so the endTick early
// return never reaches the per-group draw loop.
TEST(SpritesToScreenSort, EmptyMakesZeroGroups) {
    std::vector<SpriteRenderEntry> entries;
    auto groups = SpritesToScreenSystem::buildGroups(entries);
    EXPECT_TRUE(groups.empty());
}

// Stability: equal-depth sprites within a texture run keep their relative
// build order. std::sort is not stable, so this test pins the
// (textureHandle, isoDepth) tie-break to "any deterministic order" — the
// exact tie order is implementation-defined and not load-bearing.
TEST(SpritesToScreenSort, TieDepthEntriesStillGroup) {
    std::vector<SpriteRenderEntry> entries{
        makeEntry(1, 10),
        makeEntry(1, 10),
        makeEntry(1, 10),
        makeEntry(2, 10),
        makeEntry(2, 10),
    };
    std::sort(entries.begin(), entries.end(), &SpritesToScreenSystem::entryLess);
    auto groups = SpritesToScreenSystem::buildGroups(entries);
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0].count_, 3u);
    EXPECT_EQ(groups[1].count_, 2u);
}

// C_Sprite::size_ is in game pixels. The quad fixtures and every expected
// value are exactly representable, so the comparisons are exact.
using QuadRect = std::array<float, 4>;

const IRMath::vec2 kQuadAnchorScreen{640.0f, 360.0f};
const IRMath::vec2 kQuadSizeGamePx{16.0f, 24.0f};

QuadRect quadRect(IRMath::vec2 anchorUv, IRMath::vec2 outputScale, float screenYDirection) {
    const IRMath::vec4 quad = SpritesToScreenSystem::quadScreenRect(
        kQuadAnchorScreen,
        kQuadSizeGamePx,
        anchorUv,
        outputScale,
        screenYDirection
    );
    return {quad.x, quad.y, quad.z, quad.w};
}

// At output scale 1 a game pixel is a viewport pixel: the quad keeps the
// component's size and the anchor offset is the unscaled `anchor * size`.
TEST(SpritesToScreenQuad, UnitScaleKeepsGamePixelSizeAndOffset) {
    EXPECT_EQ(
        quadRect(IRMath::vec2{0.5f, 0.0f}, IRMath::vec2{1.0f, 1.0f}, -1.0f),
        (QuadRect{632.0f, 360.0f, 16.0f, 24.0f})
    );
}

TEST(SpritesToScreenQuad, ScaleTwoDoublesSizeAndAnchorOffset) {
    EXPECT_EQ(
        quadRect(IRMath::vec2{0.5f, 0.0f}, IRMath::vec2{2.0f, 2.0f}, -1.0f),
        (QuadRect{624.0f, 360.0f, 32.0f, 48.0f})
    );
}

// A stretched fit carries a different factor per axis; anchor (1, 1) puts
// both axes' offsets in play so a swapped or scalar factor shows up.
TEST(SpritesToScreenQuad, UnequalScaleAppliesPerAxis) {
    EXPECT_EQ(
        quadRect(IRMath::vec2{1.0f, 1.0f}, IRMath::vec2{2.0f, 3.0f}, -1.0f),
        (QuadRect{608.0f, 288.0f, 32.0f, 72.0f})
    );
}

// A zero anchor has no offset to scale: the origin stays on the anchor
// point while the size still follows the output scale.
TEST(SpritesToScreenQuad, OriginAnchorScalesSizeOnly) {
    EXPECT_EQ(
        quadRect(IRMath::vec2{0.0f, 0.0f}, IRMath::vec2{2.0f, 2.0f}, -1.0f),
        (QuadRect{640.0f, 360.0f, 32.0f, 48.0f})
    );
}

TEST(SpritesToScreenQuad, BottomOriginAnchorUsesBackendVerticalDirection) {
    EXPECT_EQ(
        quadRect(IRMath::vec2{0.5f, 0.0f}, IRMath::vec2{1.0f, 1.0f}, -1.0f),
        (QuadRect{632.0f, 360.0f, 16.0f, 24.0f})
    );
    EXPECT_EQ(
        quadRect(IRMath::vec2{0.5f, 0.0f}, IRMath::vec2{1.0f, 1.0f}, 1.0f),
        (QuadRect{632.0f, 336.0f, 16.0f, 24.0f})
    );
}

TEST(SpritesToScreenQuad, TopOriginAnchorUsesBackendVerticalDirection) {
    EXPECT_EQ(
        quadRect(IRMath::vec2{0.5f, 1.0f}, IRMath::vec2{1.0f, 1.0f}, -1.0f),
        (QuadRect{632.0f, 336.0f, 16.0f, 24.0f})
    );
    EXPECT_EQ(
        quadRect(IRMath::vec2{0.5f, 1.0f}, IRMath::vec2{1.0f, 1.0f}, 1.0f),
        (QuadRect{632.0f, 360.0f, 16.0f, 24.0f})
    );
}

using TrixelToFramebufferSystem = IRSystem::System<IRSystem::TRIXEL_TO_FRAMEBUFFER>;

const IRMath::ivec2 kPlacementGameResolution{1280, 720};

struct ZoomRaster {
    float zoom_;
    int density_;
};

// Screen position, at output scale 1 and in `screenAnchor`'s convention, of
// the canvas vertex where a unit voxel's three drawn faces meet: the main
// canvas's X1 origin plus the voxel's iso position at the raster density,
// carried back through the gather's pixel-to-canvas map and out of the
// framebuffer's extra-pixel margin. The camera sits at iso zero.
//
// @p worldPos must project to iso x = 0. The framebuffer's horizontal
// direction is not modelled, and the screen centre column is the one it
// cannot move.
IRMath::vec2 voxelFaceVertexScreen(IRMath::vec3 worldPos, ZoomRaster raster) {
    using IRMath::ivec2;
    using IRMath::vec2;
    const ivec2 framebufferSize = kPlacementGameResolution + IRConstants::kSizeExtraPixelBuffer;
    const ivec2 logicalSize =
        ivec2(IRMath::gameResolutionToSize2DIso(IRMath::uvec2(framebufferSize), IRMath::uvec2(1)));
    const ivec2 canvasSize =
        IRMath::trixelCanvasBackingSize(logicalSize, vec2(raster.zoom_), raster.density_);
    const vec2 canvasZoom = vec2(raster.zoom_) / static_cast<float>(raster.density_);
    const vec2 gatherTranslation = TrixelToFramebufferSystem::canvasGatherTranslation(
        nullptr,
        IRComponents::C_TrixelCanvasRenderBehavior{},
        vec2(0.0f),
        canvasZoom,
        raster.density_
    );
    IRRender::FrameDataTrixelToFramebuffer frame{};
    IRRender::placeGatherQuad(
        frame,
        vec2(framebufferSize),
        vec2(framebufferSize) * 0.5f + gatherTranslation,
        vec2(framebufferSize) * canvasZoom * (vec2(canvasSize) / vec2(logicalSize)),
        vec2(canvasSize)
    );

    const vec2 iso = IRMath::pos3DtoPos2DIso(worldPos);
    EXPECT_EQ(iso.x, 0.0f);
    const vec2 canvasPoint =
        vec2(IRMath::trixelOriginOffsetX1(canvasSize)) + iso * static_cast<float>(raster.density_);
    // Inverse of the gather fragment's `canvasPositionFromPixel`; framebuffer
    // rows count up, canvas rows count down.
    const vec2 framebufferPixel =
        vec2(frame.canvasFromPixel_) + (canvasPoint - vec2(canvasSize) * 0.5f) * vec2(1.0f, -1.0f) /
                                           vec2(frame.canvasFromPixel_.z, frame.canvasFromPixel_.w);
    const vec2 gamePixel = framebufferPixel - vec2(IRConstants::kSizeExtraPixelBuffer) * 0.5f;
    const vec2 viewCenter = vec2(kPlacementGameResolution) * 0.5f;
    return viewCenter + (gamePixel - viewCenter) * vec2(1.0f, -IRPlatform::kGfx.screenYDirection_);
}

IRMath::vec2 spriteAnchorScreen(IRMath::vec3 worldPos, ZoomRaster raster) {
    return SpritesToScreenSystem::screenAnchor(
        worldPos,
        IRMath::vec2(kPlacementGameResolution),
        IRMath::vec2(0.0f),
        IRMath::calcTriangleStepSizeScreen(IRMath::vec2(raster.zoom_), IRMath::ivec2(1))
    );
}

// The sprite path and the voxel path reach the screen through different
// arithmetic: a sprite's anchor from the triangle step, a voxel's faces from
// its canvas texel and the gather quad. A default-anchor sprite stands on the
// voxel at its world position only while the two agree, and the world origin
// stays on the screen centre at every zoom only while neither carries a
// zoom-dependent offset.
TEST(SpritesToScreenAnchor, MatchesVoxelFaceVertexAtZoomOneAndTwo) {
    const IRMath::vec3 worldOrigin{0.0f, 0.0f, 0.0f};
    const IRMath::vec3 belowOrigin{0.0f, 0.0f, 2.0f};
    const IRMath::vec2 screenCenter = IRMath::vec2(kPlacementGameResolution) * 0.5f;

    for (const ZoomRaster raster : {
             ZoomRaster{1.0f, 1},
             ZoomRaster{2.0f, 2},
             ZoomRaster{2.0f, 1},
         }) {
        SCOPED_TRACE(
            testing::Message() << "zoom=" << raster.zoom_ << " density=" << raster.density_
        );
        EXPECT_EQ(spriteAnchorScreen(worldOrigin, raster), screenCenter);
        EXPECT_EQ(voxelFaceVertexScreen(worldOrigin, raster), screenCenter);
        // World +z is iso y +2 per unit, and one iso row is `zoom` game pixels.
        const IRMath::vec2 expected =
            screenCenter +
            IRMath::vec2{0.0f, 4.0f * raster.zoom_ * IRPlatform::kGfx.screenYDirection_};
        EXPECT_EQ(spriteAnchorScreen(belowOrigin, raster), expected);
        EXPECT_EQ(voxelFaceVertexScreen(belowOrigin, raster), expected);
    }
}

} // namespace
