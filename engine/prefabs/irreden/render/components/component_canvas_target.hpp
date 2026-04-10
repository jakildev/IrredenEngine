#ifndef COMPONENT_CANVAS_TARGET_H
#define COMPONENT_CANVAS_TARGET_H

// PURPOSE: Lightweight per-entity reference to which canvas an object
//   should render on. Entities without this component default to the
//   "main" canvas. Intended to replace the canvasEntity_ field that
//   currently lives directly on C_ShapeDescriptor.
// STATUS: WIP stub -- defined but not used by any system or entity.
// TODO:
//   - Adopt in SHAPES_TO_TRIXEL and VOXEL_TO_TRIXEL systems as the
//     canonical way to route entities to canvases.
//   - Remove redundant canvasEntity_ from C_ShapeDescriptor once
//     this component is in use.
// DEPENDENCIES: IREntity (EntityId).

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
