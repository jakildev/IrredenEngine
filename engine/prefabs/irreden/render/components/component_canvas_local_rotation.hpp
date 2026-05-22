#ifndef COMPONENT_CANVAS_LOCAL_ROTATION_H
#define COMPONENT_CANVAS_LOCAL_ROTATION_H

#include <irreden/ir_math.hpp>

namespace IRComponents {

// Per-entity SO(3) rotation propagated onto a voxel-pool canvas entity by
// PROPAGATE_CANVAS_ROTATION — for a detached canvas, copied each tick from the
// parent entity's C_LocalTransform. VOXEL_TO_TRIXEL_STAGE_1 reads it and bakes
// full SO(3) rotation into the voxel emit via IRMath::faceDeformationMatrixSO3
// (T-295), rasterizing the detached voxels in the entity's own model space.
//
// The default (0,0,0,0) — an all-zero, non-unit quaternion — is the
// "not a detached canvas" sentinel: it is never a valid rotation, so the
// main world canvas (which PROPAGATE_CANVAS_ROTATION never writes) keeps it
// and VOXEL_TO_TRIXEL_STAGE_1 falls back to the camera-residual-yaw path,
// preserving T-293's cardinal-yaw byte-identity. PROPAGATE_CANVAS_ROTATION
// always overwrites it with a unit quaternion — including identity
// (0,0,0,1) when the detached entity is unrotated. Layout matches IRMath /
// C_LocalTransform: vec4(qx, qy, qz, qw).
struct C_CanvasLocalRotation {
    IRMath::vec4 rotation_ = IRMath::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    C_CanvasLocalRotation() = default;
    explicit C_CanvasLocalRotation(IRMath::vec4 rotation)
        : rotation_{rotation} {}
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_LOCAL_ROTATION_H */
