#ifndef COMPONENT_CANVAS_RESIDENCY_H
#define COMPONENT_CANVAS_RESIDENCY_H

#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_rotation_mode.hpp>

namespace IRComponents {

// Opts an entity into canvas residency: CANVAS_RESIDENCY gives it an entity
// canvas while it is inside the camera's interest region and returns it to
// GRID outside, so a world can author far more detached entities than it has
// canvases for. The fields are what the canvas is rebuilt from on each
// re-entry; zero sizes derive from the entity's own voxel set.
struct C_CanvasResidency {
    // The canvas-owning mode the entity renders in while resident.
    RotationMode residentMode_ = RotationMode::DETACHED_REVOXELIZE;
    IRMath::ivec2 canvasSize_{0, 0};
    // A host of canvas parts names the pool extent spanning all of them.
    IRMath::ivec3 poolSize_{0, 0, 0};
    bool screenLocked_ = false;
    int depthPriority_ = 0;
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_RESIDENCY_H */
