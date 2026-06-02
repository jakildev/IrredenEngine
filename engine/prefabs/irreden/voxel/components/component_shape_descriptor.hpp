#ifndef COMPONENT_SHAPE_DESCRIPTOR_H
#define COMPONENT_SHAPE_DESCRIPTOR_H

#include <irreden/ir_math.hpp>
#include <irreden/math/sdf.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/lod_level.hpp>

using namespace IRMath;

namespace IRComponents {

// Preferred rendering component for game entities. The GPU evaluates the
// shape's SDF directly -- no per-voxel allocation, no dirty tracking.
// Rendered by SHAPES_TO_TRIXEL (two-pass distance + color/entityID).
//
// flags_ values (see ShapeFlags in IRMath::SDF):
//   SHAPE_FLAG_VISIBLE           - shape is rendered (default on)
//   SHAPE_FLAG_HOLLOW            - only render the shell of the SDF
//   SHAPE_FLAG_MIRROR_X/Y        - mirror the shape along an axis
//
// lodMin_ / lodMax_ bound the inclusive LOD band this shape draws in.
// lodMin_ is the coarsest tier (largest index) and lodMax_ the finest tier
// (smallest index) at which it still renders; the tier index goes down as
// detail goes up. SHAPES_TO_TRIXEL reads the C_ActiveLodLevel singleton at
// beginTick and draws the shape iff lodMax_ <= activeLod <= lodMin_ (CPU-side
// skip, pre-GPU staging — see lod_utils.hpp::shouldSkipAtLod).
//
// The defaults (lodMin_ = LOD_4, lodMax_ = LOD_0) span the whole range, so an
// unmarked shape is always visible — byte-identical to the pre-band filter.
// Confining a shape to a sub-band makes it a LOD variant: a set of co-located
// variants with disjoint bands renders exclusively (exactly one per zoom,
// swapping rather than stacking — the #1467 fix). Keep the coarsest variant at
// lodMin_ = LOD_4 (persists at min zoom) and the finest at lodMax_ = LOD_0
// (persists past its threshold). See docs/design/lod-strategy.md.
struct C_ShapeDescriptor {
    IRMath::SDF::ShapeType shapeType_ = IRMath::SDF::ShapeType::BOX;
    vec4 params_ = vec4(1.0f, 1.0f, 1.0f, 0.0f);
    Color color_ = Color{255, 255, 255, 255};
    std::uint32_t flags_ = IRMath::SDF::SHAPE_FLAG_VISIBLE;
    IRRender::LodLevel lodMin_ = IRRender::LodLevel::LOD_4;
    IRRender::LodLevel lodMax_ = IRRender::LodLevel::LOD_0;
    IREntity::EntityId canvasEntity_ = IREntity::kNullEntity;

    C_ShapeDescriptor()
        : canvasEntity_{IRRender::getActiveCanvasEntityOrNull()} {}

    C_ShapeDescriptor(IRMath::SDF::ShapeType type, vec4 params, Color color)
        : shapeType_{type}
        , params_{params}
        , color_{color}
        , canvasEntity_{IRRender::getActiveCanvasEntityOrNull()} {}
};

} // namespace IRComponents

#endif /* COMPONENT_SHAPE_DESCRIPTOR_H */
