#include <gtest/gtest.h>

#include <irreden/render/systems/system_sprites_to_screen.hpp>
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

QuadRect quadRect(IRMath::vec2 anchorUv, IRMath::vec2 outputScale) {
    const IRMath::vec4 quad = SpritesToScreenSystem::quadScreenRect(
        kQuadAnchorScreen,
        kQuadSizeGamePx,
        anchorUv,
        outputScale
    );
    return {quad.x, quad.y, quad.z, quad.w};
}

// At output scale 1 a game pixel is a viewport pixel: the quad keeps the
// component's size and the anchor offset is the unscaled `anchor * size`.
TEST(SpritesToScreenQuad, UnitScaleKeepsGamePixelSizeAndOffset) {
    EXPECT_EQ(
        quadRect(IRMath::vec2{0.5f, 0.0f}, IRMath::vec2{1.0f, 1.0f}),
        (QuadRect{632.0f, 360.0f, 16.0f, 24.0f})
    );
}

TEST(SpritesToScreenQuad, ScaleTwoDoublesSizeAndAnchorOffset) {
    EXPECT_EQ(
        quadRect(IRMath::vec2{0.5f, 0.0f}, IRMath::vec2{2.0f, 2.0f}),
        (QuadRect{624.0f, 360.0f, 32.0f, 48.0f})
    );
}

// A stretched fit carries a different factor per axis; anchor (1, 1) puts
// both axes' offsets in play so a swapped or scalar factor shows up.
TEST(SpritesToScreenQuad, UnequalScaleAppliesPerAxis) {
    EXPECT_EQ(
        quadRect(IRMath::vec2{1.0f, 1.0f}, IRMath::vec2{2.0f, 3.0f}),
        (QuadRect{608.0f, 288.0f, 32.0f, 72.0f})
    );
}

// A zero anchor has no offset to scale: the origin stays on the anchor
// point while the size still follows the output scale.
TEST(SpritesToScreenQuad, OriginAnchorScalesSizeOnly) {
    EXPECT_EQ(
        quadRect(IRMath::vec2{0.0f, 0.0f}, IRMath::vec2{2.0f, 2.0f}),
        (QuadRect{640.0f, 360.0f, 32.0f, 48.0f})
    );
}

} // namespace
