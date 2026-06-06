#ifndef COMPONENT_CANVAS_LOCAL_ROTATION_H
#define COMPONENT_CANVAS_LOCAL_ROTATION_H

#include <irreden/ir_math.hpp>

namespace IRComponents {

// Per-entity SO(3) rotation propagated onto a voxel-pool canvas entity by
// PROPAGATE_CANVAS_ROTATION — for a detached canvas, composed each tick as
// `quatInverse(R_camera) * entityRotation` where R_camera is the world-camera
// rotation snapshot at begin-tick (T-319). This bakes the entity rotation in
// the camera's frame so the canvas content tracks the world as the camera
// rotates, mirroring GRID-mode behavior. VOXEL_TO_TRIXEL_STAGE_1 reads it and
// bakes full SO(3) rotation into the voxel emit via
// IRMath::faceDeformationMatrixSO3 (T-295), rasterizing the detached voxels
// in the camera-composed model space.
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
    // All-zero, non-unit quaternion used as the "not a detached canvas" sentinel.
    // PROPAGATE_CANVAS_ROTATION always overwrites with a unit quaternion (including
    // identity (0,0,0,1) for an unrotated detached entity), so this value is only
    // ever present on the world canvas which PROPAGATE_CANVAS_ROTATION never touches.
    static constexpr IRMath::vec4 kSentinelNoRotation{0.0f, 0.0f, 0.0f, 0.0f};

    IRMath::vec4 rotation_ = kSentinelNoRotation;

    // Rotation-strategy flag (NOT a dirty flag — a persistent per-canvas mode
    // bit, set by PROPAGATE_CANVAS_ROTATION from the owner's C_RotationMode and
    // read every frame, analogous to C_RotationMode::mode_). When true the
    // detached entity rotates by RE-VOXELIZING its private pool
    // (RotationMode::DETACHED_REVOXELIZE): SYSTEM_REBUILD_DETACHED_VOXELS fills
    // the pool at `rotation_`'s full-rotation cell positions and
    // VOXEL_TO_TRIXEL_STAGE_1 rasterizes it through CARDINAL frame data (no SO(3)
    // face deform — applying the rotation twice would re-introduce the 2D warp,
    // #1551). When false the canvas takes the octahedral-snap forward-scatter /
    // per-face-deform path. Inert on the main world canvas (sentinel rotation).
    bool reVoxelize_ = false;

    C_CanvasLocalRotation() = default;
    explicit C_CanvasLocalRotation(IRMath::vec4 rotation)
        : rotation_{rotation} {}

    bool isDetached() const noexcept {
        return rotation_.x != 0.0f || rotation_.y != 0.0f || rotation_.z != 0.0f ||
               rotation_.w != 0.0f;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_LOCAL_ROTATION_H */
