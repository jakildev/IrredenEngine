#ifndef IR_RENDER_SHAPE_TILE_DOMAIN_H
#define IR_RENDER_SHAPE_TILE_DOMAIN_H

#include <irreden/ir_math.hpp>
#include <irreden/render/ir_render_types.hpp>

namespace IRSystem {

// The shapes kernel discards a sample whose base canvas pixel lies more than
// this many pixels outside the canvas (the `baseCanvasPixel` guard in
// c_shapes_to_trixel_body.{glsl,metal}); its sub-pixel emit offsets reach back
// into the canvas from that ring.
constexpr int kShapeCanvasGuardPixels = 3;

// Inclusive iso-pixel rectangle.
struct ShapeIsoRect {
    IRMath::ivec2 min_;
    IRMath::ivec2 max_;
};

// The camera Z-yaw SHAPES_TO_TRIXEL snapshots at beginTick and rasterizes a
// canvas at: the cardinal rasterYaw_ with its exact cos/sin, and the
// continuous visualYaw_ the smooth-yaw path projects at.
struct ShapeTileYaw {
    float rasterYaw_ = 0.0f;
    float yawCos_ = 1.0f;
    float yawSin_ = 0.0f;
    bool smoothYaw_ = false;
    float visualYaw_ = 0.0f;
    float yawCosVisual_ = 1.0f;
    float yawSinVisual_ = 0.0f;
};

// A shape's dispatch tiles: tile (tx, ty) covers the iso pixels
// isoOrigin_ + tileSize * (tx, ty) + [0, tileSize)^2, for first_ <= (tx, ty) < end_.
struct ShapeTileSpan {
    IRMath::ivec2 isoOrigin_;
    IRMath::ivec2 first_;
    IRMath::ivec2 end_;

    int count() const {
        const IRMath::ivec2 tiles = IRMath::max(end_ - first_, IRMath::ivec2(0));
        return tiles.x * tiles.y;
    }
};

// Conservative iso footprint of one shape at subdivision scale @p sub, in the
// frame the shader rasterizes it in. The max_ corner is where the tile grid
// stops, not a covered pixel. Cardinal path: the origin snaps to the rasterYaw
// view cell. Smooth path: centered on the full-visualYaw projection (the
// shader's originIsoScaled) and grown by the continuous |cos|,|sin| up to the
// sqrt(2) extent; a lattice shape at density 1 anchors on its snapped view
// cell instead, the origin the kernel's lattice walk keys its parity on.
inline ShapeIsoRect shapeTileIsoBounds(
    const IRRender::GPUShapeDescriptor &desc, int sub, const ShapeTileYaw &yaw, bool latticeShapes
) {
    using namespace IRMath;
    const vec3 worldPos = vec3(desc.worldPosition);
    // Canonical bounding half-extent lives in IRMath::SDF (shared with the
    // lighting / shadow pipeline).
    vec3 boundingHalf = SDF::boundingHalf(static_cast<SDF::ShapeType>(desc.shapeType), desc.params);
    if (IRMath::abs(desc.rotation.w) < 0.9999f) {
        const vec3 ax = IRMath::abs(rotateVectorByQuat(vec3(boundingHalf.x, 0, 0), desc.rotation));
        const vec3 ay = IRMath::abs(rotateVectorByQuat(vec3(0, boundingHalf.y, 0), desc.rotation));
        const vec3 az = IRMath::abs(rotateVectorByQuat(vec3(0, 0, boundingHalf.z), desc.rotation));
        boundingHalf = ax + ay + az;
    }
    if (yaw.smoothYaw_) {
        boundingHalf = yawGrownIsoHalfExtent(boundingHalf, yaw.yawCosVisual_, yaw.yawSinVisual_);
        ivec2 originIsoScaled;
        if (latticeShapes && sub == 1) {
            const vec3 viewPosYawed(
                yaw.yawCosVisual_ * worldPos.x + yaw.yawSinVisual_ * worldPos.y,
                -yaw.yawSinVisual_ * worldPos.x + yaw.yawCosVisual_ * worldPos.y,
                worldPos.z
            );
            originIsoScaled = pos3DtoPos2DIso(roundVec3HalfUp(viewPosYawed));
        } else {
            const vec2 originIsoF =
                pos3DtoPos2DIsoYawed(worldPos * static_cast<float>(sub), yaw.visualYaw_);
            originIsoScaled = ivec2(roundHalfUp(originIsoF.x), roundHalfUp(originIsoF.y));
        }
        const ivec2 isoHalfExtent = ivec2(shapeIsoHalfExtent(boundingHalf * 2.0f)) * sub;
        return {
            originIsoScaled - isoHalfExtent - ivec2(2),
            originIsoScaled + isoHalfExtent + ivec2(2)
        };
    }
    // Z-yaw expands the XY AABB by |c|·hX + |s|·hY (and symmetric).
    if (yaw.rasterYaw_ != 0.0f) {
        boundingHalf = yawGrownIsoHalfExtent(boundingHalf, yaw.yawCos_, yaw.yawSin_);
    }
    const CardinalIndex cardinalIndex = rasterYawCardinalIndex(yaw.rasterYaw_);
    const ivec2 originIso =
        pos3DtoPos2DIso(roundVec3HalfUp(rotateCardinalZ(worldPos, cardinalIndex)));
    const ivec2 isoHalfExtent = ivec2(shapeIsoHalfExtent(boundingHalf * 2.0f));
    return {
        (originIso - isoHalfExtent) * sub - ivec2(2),
        (originIso + isoHalfExtent) * sub + ivec2(2)
    };
}

// Iso pixels whose samples the shapes kernel can write on a canvas: the
// shader's frame offset (`trixelFrameOffset`) maps iso pixel p to canvas pixel
// frameOffset + p, kept within kShapeCanvasGuardPixels of the canvas. One extra
// pixel per side absorbs a CPU/GPU disagreement in floor(cameraTrixelOffset * scale).
inline ShapeIsoRect shapeCanvasReachableIso(
    IRMath::ivec2 trixelCanvasOffsetZ1,
    IRMath::vec2 cameraTrixelOffset,
    int subdivisionScale,
    IRMath::ivec2 canvasSize
) {
    using namespace IRMath;
    const ivec2 frameOffset =
        trixelCanvasOffsetZ1 +
        ivec2(
            IRMath::floor(cameraTrixelOffset * static_cast<float>(IRMath::max(subdivisionScale, 1)))
        );
    const ivec2 margin(kShapeCanvasGuardPixels + 1);
    return {-margin - frameOffset, canvasSize - ivec2(1) + margin - frameOffset};
}

// The tiles of @p bounds that touch @p reachable, on the shape's own tile grid:
// whole tiles are dropped, never re-anchored, so a clipped shape keeps the tile
// origins and relative tile order (the sample-owner election's tie order) it
// has unclipped.
inline ShapeTileSpan
clipShapeTiles(const ShapeIsoRect &bounds, const ShapeIsoRect &reachable, int tileSize) {
    using namespace IRMath;
    const ivec2 size = IRMath::max(bounds.max_ - bounds.min_, ivec2(1));
    const ivec2 tiles(divCeil(size.x, tileSize), divCeil(size.y, tileSize));
    const ivec2 lo = reachable.min_ - bounds.min_;
    const ivec2 hi = reachable.max_ - bounds.min_;
    const ivec2 first(
        static_cast<int>(floorDiv(lo.x, tileSize)),
        static_cast<int>(floorDiv(lo.y, tileSize))
    );
    const ivec2 last(
        static_cast<int>(floorDiv(hi.x, tileSize)),
        static_cast<int>(floorDiv(hi.y, tileSize))
    );
    return {bounds.min_, IRMath::max(first, ivec2(0)), IRMath::min(last + ivec2(1), tiles)};
}

} // namespace IRSystem

#endif // IR_RENDER_SHAPE_TILE_DOMAIN_H
