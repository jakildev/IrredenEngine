#ifndef COMPONENT_SHAPE_DESCRIPTOR_H
#define COMPONENT_SHAPE_DESCRIPTOR_H

#include <irreden/ir_math.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/ir_render_types.hpp>

using namespace IRMath;

namespace IRComponents {

// Preferred rendering component for game entities. The GPU evaluates the
// shape's SDF directly -- no per-voxel allocation, no dirty tracking.
// Rendered by SHAPES_TO_TRIXEL (two-pass distance + color/entityID).
//
// flags_ values (see ShapeFlags in ir_render_types.hpp):
//   SHAPE_FLAG_VISIBLE           - shape is rendered (default on)
//   SHAPE_FLAG_HOLLOW            - only render the shell of the SDF
//   SHAPE_FLAG_MIRROR_X/Y        - mirror the shape along an axis
//   SHAPE_FLAG_DISCRETE_ROTATION - (future) snap joint rotation to 90-deg
//                                  increments in iso-adjusted coordinates
//
// lodMin_ is the coarsest LOD tier at which this shape still renders —
// equivalently, the smallest LodLevel index it's visible at, since the
// tier index goes down as detail goes up. A shape with lodMin_ = LOD_0
// renders only when the camera is zoomed all the way in (activeLod ==
// LOD_0); a shape with lodMin_ = LOD_4 (the default) is always visible
// because every activeLod satisfies activeLod <= LOD_4. SHAPES_TO_TRIXEL
// reads the C_ActiveLodLevel singleton at beginTick and skips shapes
// where lodMin_ < activeLod (CPU-side, pre-GPU staging). See
// docs/design/lod-strategy.md and engine/prefabs/irreden/render/lod_utils.hpp.
struct C_ShapeDescriptor {
    IRRender::ShapeType shapeType_ = IRRender::ShapeType::BOX;
    vec4 params_ = vec4(1.0f, 1.0f, 1.0f, 0.0f);
    Color color_ = Color{255, 255, 255, 255};
    std::uint32_t flags_ = IRRender::SHAPE_FLAG_VISIBLE;
    IRRender::LodLevel lodMin_ = IRRender::LodLevel::LOD_4;
    IREntity::EntityId canvasEntity_ = IREntity::kNullEntity;

    C_ShapeDescriptor()
        : canvasEntity_{IRRender::getActiveCanvasEntityOrNull()} {}

    C_ShapeDescriptor(IRRender::ShapeType type, vec4 params, Color color)
        : shapeType_{type}
        , params_{params}
        , color_{color}
        , canvasEntity_{IRRender::getActiveCanvasEntityOrNull()} {}
};

} // namespace IRComponents

#endif /* COMPONENT_SHAPE_DESCRIPTOR_H */
