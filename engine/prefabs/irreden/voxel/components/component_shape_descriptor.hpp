#ifndef COMPONENT_SHAPE_DESCRIPTOR_H
#define COMPONENT_SHAPE_DESCRIPTOR_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

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
struct C_ShapeDescriptor {
    IRRender::ShapeType shapeType_ = IRRender::ShapeType::BOX;
    vec4 params_ = vec4(1.0f, 1.0f, 1.0f, 0.0f);
    Color color_ = Color{255, 255, 255, 255};
    std::uint32_t flags_ = IRRender::SHAPE_FLAG_VISIBLE;
    std::uint32_t lodLevel_ = 0;
    IREntity::EntityId canvasEntity_ = IREntity::kNullEntity;

    C_ShapeDescriptor()
        : canvasEntity_{IRRender::getActiveCanvasEntity()} {}

    C_ShapeDescriptor(IRRender::ShapeType type, vec4 params, Color color)
        : shapeType_{type}
        , params_{params}
        , color_{color}
        , canvasEntity_{IRRender::getActiveCanvasEntity()} {}
};

} // namespace IRComponents

#endif /* COMPONENT_SHAPE_DESCRIPTOR_H */
