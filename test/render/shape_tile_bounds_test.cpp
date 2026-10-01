// SHAPES_TO_TRIXEL tile-domain clipping, with no GPU.
//
// The fixtures are the shape_debug floor box at zoom 16 (subdivision 16) on
// the 642x722 main canvas, at two poses of the deterministic
// `--spin-yaw --zoom 16 --auto-screenshot 720` sweep: shot 623 (smooth yaw,
// residual near 45 degrees) and shot 180 (cardinal 90 degrees). The camera
// offsets and expected unclipped footprints are the running system's values at
// those poses.

#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>
#include <irreden/render/shape_tile_domain.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>

using IRMath::ivec2;
using IRMath::vec2;
using IRMath::vec4;
using IRRender::GPUShapeDescriptor;
using IRSystem::clipShapeTiles;
using IRSystem::kMaxShapeTileDescriptors;
using IRSystem::kShapeCanvasGuardPixels;
using IRSystem::kShapeTileSize;
using IRSystem::shapeCanvasReachableIso;
using IRSystem::ShapeIsoRect;
using IRSystem::shapeTileIsoBounds;
using IRSystem::ShapeTileSpan;
using IRSystem::ShapeTileYaw;

namespace {

constexpr int kSub = 16;
const ivec2 kCanvasSize(642, 722);
const ivec2 kCanvasOffsetZ1(320, 360);

GPUShapeDescriptor floorBox() {
    GPUShapeDescriptor desc{};
    desc.worldPosition = vec4(56.0f, 6.0f, 5.0f, 0.0f);
    desc.params = vec4(144.0f, 36.0f, 2.0f, 0.0f);
    desc.rotation = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    desc.shapeType = static_cast<std::uint32_t>(IRMath::SDF::ShapeType::BOX);
    return desc;
}

ShapeTileYaw smoothYaw(float rasterYaw, float rasterCos, float rasterSin, float visualYaw) {
    return ShapeTileYaw{
        rasterYaw,
        rasterCos,
        rasterSin,
        true,
        visualYaw,
        IRMath::cos(visualYaw),
        IRMath::sin(visualYaw)
    };
}

ShapeTileYaw cardinalYaw(float rasterYaw, float rasterCos, float rasterSin) {
    return ShapeTileYaw{rasterYaw, rasterCos, rasterSin, false, rasterYaw, rasterCos, rasterSin};
}

ShapeTileSpan unclippedSpan(const ShapeIsoRect &bounds) {
    const ivec2 size = IRMath::max(bounds.max_ - bounds.min_, ivec2(1));
    return {
        bounds.min_,
        ivec2(0),
        ivec2(IRMath::divCeil(size.x, kShapeTileSize), IRMath::divCeil(size.y, kShapeTileSize))
    };
}

ShapeIsoRect tilePixels(const ShapeTileSpan &span) {
    return {
        span.isoOrigin_ + span.first_ * kShapeTileSize,
        span.isoOrigin_ + span.end_ * kShapeTileSize - ivec2(1)
    };
}

// The iso pixels the kernel keeps: its frame offset is
// trixelCanvasOffsetZ1 + floor(frameCanvasOffset * scale), and it discards a
// sample whose base canvas pixel is not in [-3, canvasSize + 3).
ShapeIsoRect shaderKeptIso(vec2 cameraTrixelOffset) {
    const ivec2 frameOffset =
        kCanvasOffsetZ1 + ivec2(IRMath::floor(cameraTrixelOffset * static_cast<float>(kSub)));
    return {
        ivec2(-kShapeCanvasGuardPixels) - frameOffset,
        kCanvasSize + ivec2(kShapeCanvasGuardPixels - 1) - frameOffset
    };
}

void expectCoversKeptPixels(const ShapeIsoRect &bounds, vec2 cameraTrixelOffset) {
    const ShapeTileSpan clipped = clipShapeTiles(
        bounds,
        shapeCanvasReachableIso(kCanvasOffsetZ1, cameraTrixelOffset, kSub, kCanvasSize),
        kShapeTileSize
    );
    const ShapeIsoRect all = tilePixels(unclippedSpan(bounds));
    const ShapeIsoRect kept = shaderKeptIso(cameraTrixelOffset);
    const ShapeIsoRect need{IRMath::max(all.min_, kept.min_), IRMath::min(all.max_, kept.max_)};
    ASSERT_TRUE(need.min_.x <= need.max_.x && need.min_.y <= need.max_.y);

    ASSERT_GT(clipped.count(), 0);
    const ShapeIsoRect got = tilePixels(clipped);
    EXPECT_LE(got.min_.x, need.min_.x);
    EXPECT_LE(got.min_.y, need.min_.y);
    EXPECT_GE(got.max_.x, need.max_.x);
    EXPECT_GE(got.max_.y, need.max_.y);
    // Every emitted tile touches the kept rectangle (plus the one-pixel slack
    // for the frame-offset floor): no off-canvas work survives.
    EXPECT_GE(got.min_.x + kShapeTileSize, kept.min_.x - 1);
    EXPECT_GE(got.min_.y + kShapeTileSize, kept.min_.y - 1);
    EXPECT_LE(got.max_.x - kShapeTileSize, kept.max_.x + 1);
    EXPECT_LE(got.max_.y - kShapeTileSize, kept.max_.y + 1);
    // Tile origins stay on the shape's own grid.
    EXPECT_EQ(clipped.isoOrigin_, bounds.min_);
}

} // namespace

TEST(ShapeTileBounds, ZoomedFloorAtResidualYawFitsTheBudgetOnlyOnceClipped) {
    const vec2 camera(5.07951975f, 2.4927206f);
    const ShapeIsoRect bounds = shapeTileIsoBounds(
        floorBox(),
        kSub,
        smoothYaw(-IRMath::kHalfPi, 0.0f, -1.0f, -0.846484423f),
        false
    );
    EXPECT_EQ(bounds.min_, ivec2(-1869, -3242));
    EXPECT_EQ(bounds.max_, ivec2(2295, 1050));

    const ShapeTileSpan unclipped = unclippedSpan(bounds);
    EXPECT_EQ(unclipped.end_, ivec2(521, 537));
    EXPECT_GT(unclipped.count(), kMaxShapeTileDescriptors);

    const ShapeTileSpan clipped = clipShapeTiles(
        bounds,
        shapeCanvasReachableIso(kCanvasOffsetZ1, camera, kSub, kCanvasSize),
        kShapeTileSize
    );
    EXPECT_LT(clipped.count(), kMaxShapeTileDescriptors);
    // One canvas of tiles, plus the guard ring and one partial tile per side.
    const ivec2 canvasTiles =
        (kCanvasSize + ivec2(2 * (kShapeCanvasGuardPixels + 1))) / kShapeTileSize + ivec2(2);
    EXPECT_LE(clipped.count(), canvasTiles.x * canvasTiles.y);
    expectCoversKeptPixels(bounds, camera);
}

TEST(ShapeTileBounds, CardinalYawFloorClipsToTheCanvas) {
    const vec2 camera(-6.88322067f, 6.88505888f);
    const ShapeIsoRect bounds =
        shapeTileIsoBounds(floorBox(), kSub, cardinalYaw(IRMath::kHalfPi, 0.0f, 1.0f), false);
    EXPECT_EQ(bounds.min_, ivec2(-2466, -578));
    EXPECT_EQ(bounds.max_, ivec2(482, 2498));
    EXPECT_EQ(unclippedSpan(bounds).end_, ivec2(369, 385));
    expectCoversKeptPixels(bounds, camera);
}

TEST(ShapeTileBounds, OnCanvasShapeKeepsItsWholeSpan) {
    const ShapeIsoRect bounds{ivec2(-20, -13), ivec2(20, 30)};
    const ShapeTileSpan clipped = clipShapeTiles(bounds, {ivec2(-400), ivec2(400)}, kShapeTileSize);
    const ShapeTileSpan whole = unclippedSpan(bounds);
    EXPECT_EQ(clipped.first_, whole.first_);
    EXPECT_EQ(clipped.end_, whole.end_);
    EXPECT_EQ(clipped.isoOrigin_, whole.isoOrigin_);
}

TEST(ShapeTileBounds, OffCanvasShapeEmitsNoTiles) {
    const ShapeIsoRect reachable{ivec2(-50), ivec2(50)};
    for (const ShapeIsoRect &bounds : {
             ShapeIsoRect{ivec2(51, 0), ivec2(90, 10)},
             ShapeIsoRect{ivec2(-90, 0), ivec2(-58, 10)},
             ShapeIsoRect{ivec2(0, 51), ivec2(10, 90)},
             ShapeIsoRect{ivec2(0, -90), ivec2(10, -58)},
         }) {
        EXPECT_EQ(clipShapeTiles(bounds, reachable, kShapeTileSize).count(), 0);
    }
}

TEST(ShapeTileBounds, ReachableEdgesAreInclusive) {
    // Four tiles covering iso x in [-16, 16).
    const ShapeIsoRect bounds{ivec2(-16, 0), ivec2(16, 8)};
    const auto columns = [&](int minX, int maxX) {
        const ShapeTileSpan span =
            clipShapeTiles(bounds, {ivec2(minX, 0), ivec2(maxX, 7)}, kShapeTileSize);
        return ivec2(span.first_.x, span.end_.x);
    };
    EXPECT_EQ(columns(-16, 15), ivec2(0, 4));
    EXPECT_EQ(columns(-9, 15), ivec2(0, 4));
    EXPECT_EQ(columns(-8, 15), ivec2(1, 4));
    EXPECT_EQ(columns(-16, 8), ivec2(0, 4));
    EXPECT_EQ(columns(-16, 7), ivec2(0, 3));
    EXPECT_EQ(columns(15, 40), ivec2(3, 4));
    EXPECT_EQ(clipShapeTiles(bounds, {ivec2(16, 0), ivec2(40, 7)}, kShapeTileSize).count(), 0);
    EXPECT_EQ(clipShapeTiles(bounds, {ivec2(-40, 0), ivec2(-17, 7)}, kShapeTileSize).count(), 0);
}

TEST(ShapeTileBounds, ReachableRectMatchesTheShaderGuardWithOnePixelSlack) {
    const vec2 camera(5.07951975f, 2.4927206f);
    const ShapeIsoRect reachable =
        shapeCanvasReachableIso(kCanvasOffsetZ1, camera, kSub, kCanvasSize);
    const ShapeIsoRect kept = shaderKeptIso(camera);
    EXPECT_EQ(reachable.min_, kept.min_ - ivec2(1));
    EXPECT_EQ(reachable.max_, kept.max_ + ivec2(1));
}
