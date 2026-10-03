#ifndef COMPONENT_VIEWPORT_CAMERA_H
#define COMPONENT_VIEWPORT_CAMERA_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

namespace IRComponents {

// A secondary viewport: a camera entity that views one private voxel-pool
// canvas and pins it to a GUI rectangle. The entity also carries the
// camera's `C_ZoomLevel` and its yaw in `C_LocalTransform::rotation_`; the
// entity id is the viewport id `IRPrefab::Viewport::` takes.
struct C_ViewportCamera {
    IREntity::EntityId canvasEntity_ = IREntity::kNullEntity;
    // GUI-canvas trixels, origin at the rectangle's top-left corner.
    IRMath::ivec2 rectOrigin_{0};
    IRMath::ivec2 rectSize_{0};
    // Point of the subject, in voxels from its center, shown at the
    // rectangle's center.
    IRMath::vec3 focus_{0.0f};
    bool visible_ = true;
    // The tagged entity whose id the canvas carries this frame; kNullEntity
    // while nothing is drawn. Written by SYNC_VIEWPORT_SUBJECTS.
    IREntity::EntityId drawnSubject_ = IREntity::kNullEntity;
    // True while the tagged subject is too large for the shared voxel buffers
    // and is therefore not drawn. Written by SYNC_VIEWPORT_SUBJECTS.
    bool subjectOversize_ = false;

    C_ViewportCamera() = default;

    C_ViewportCamera(
        IREntity::EntityId canvasEntity, IRMath::ivec2 rectOrigin, IRMath::ivec2 rectSize
    )
        : canvasEntity_{canvasEntity}
        , rectOrigin_{rectOrigin}
        , rectSize_{rectSize} {}
};

} // namespace IRComponents

#endif /* COMPONENT_VIEWPORT_CAMERA_H */
