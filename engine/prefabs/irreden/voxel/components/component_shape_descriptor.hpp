#ifndef COMPONENT_SHAPE_DESCRIPTOR_H
#define COMPONENT_SHAPE_DESCRIPTOR_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

using namespace IRMath;

namespace IRComponents {

// Preferred rendering component for game entities. The GPU evaluates the
// shape's SDF directly -- no per-voxel allocation, no dirty tracking.
// Rendered by SHAPES_TO_TRIXEL (two-pass distance + color/entityID).
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
