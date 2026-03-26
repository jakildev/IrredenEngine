#ifndef COMPONENT_CANVAS_TARGET_H
#define COMPONENT_CANVAS_TARGET_H

#include <irreden/ir_entity.hpp>

namespace IRComponents {

// Lightweight reference to the canvas entity an object should render on.
// Entities without this component default to the "main" canvas.
struct C_CanvasTarget {
    IREntity::EntityId canvasEntity_ = IREntity::kNullEntity;

    C_CanvasTarget() = default;
    explicit C_CanvasTarget(IREntity::EntityId canvas) : canvasEntity_{canvas} {}
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_TARGET_H */
