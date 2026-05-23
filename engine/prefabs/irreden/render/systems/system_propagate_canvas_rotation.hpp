#ifndef SYSTEM_PROPAGATE_CANVAS_ROTATION_H
#define SYSTEM_PROPAGATE_CANVAS_ROTATION_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/camera.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>

// PROPAGATE_CANVAS_ROTATION (T-295, Epic C C7) — UPDATE pipeline.
//
// Composes the inverse world-camera rotation with each DETACHED entity's
// `C_LocalTransform` rotation and writes the result onto the per-entity
// canvas as `C_CanvasLocalRotation`, so the RENDER pipeline's
// VOXEL_TO_TRIXEL_STAGE_1 bakes the result into the canvas's voxel emit
// (via IRMath::faceDeformationMatrixSO3, T-295). The camera composition
// (T-319) cancels the camera basis the world canvas already applies to
// the composited per-entity canvas, so a DETACHED entity at identity
// rotation stays stationary in camera-space as the world camera spins
// — matching GRID-mode behavior. The camera surface is just Z-yaw today;
// the full SO(3) camera grow (issue #1076) flows in transparently via
// `IRPrefab::Camera::getRotationQuat()`.
//
// The canvas child carries `C_CanvasLocalRotation` (attached at creation
// by Prefab<kVoxelPoolCanvas>), so the write is an in-place value update
// — no archetype migration — and is safe against the foreign child
// mid-tick. GRID-mode entities are skipped (the camera basis still
// reaches them through the rasterYaw / faceDeform residual path).
//
// Register in the UPDATE pipeline after PROPAGATE_TRANSFORM; it must run
// before the RENDER pipeline reads the value.

namespace IRSystem {

template <> struct System<PROPAGATE_CANVAS_ROTATION> {
    // Snapshot of the world-camera rotation for the current frame. The
    // begin-tick capture keeps the per-entity tick free of global lookups
    // and guarantees every entity in this frame sees the same camera basis.
    IRMath::vec4 cameraRotationInverse_ = IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    void beginTick() {
        cameraRotationInverse_ = IRMath::quatInverse(IRPrefab::Camera::getRotationQuat());
    }

    void tick(
        const IRComponents::C_LocalTransform &localTransform,
        const IRComponents::C_RotationMode &rotationMode,
        const IRComponents::C_EntityCanvas &entityCanvas
    ) {
        if (rotationMode.mode_ != IRComponents::RotationMode::DETACHED) {
            return;
        }
        auto canvasRotation = IREntity::getComponentOptional<IRComponents::C_CanvasLocalRotation>(
            entityCanvas.canvasEntity_
        );
        if (canvasRotation.has_value()) {
            // Compose in camera space: apply entity rotation first, then
            // un-rotate by the camera. When the world canvas later re-applies
            // the camera basis to the composited per-entity canvas, the two
            // camera factors cancel and only the entity rotation remains in
            // camera-space — DETACHED entities track the world like GRID does.
            canvasRotation.value()->rotation_ =
                IRMath::quatMul(cameraRotationInverse_, localTransform.rotation_);
        }
    }

    static SystemId create() {
        return registerSystem<
            PROPAGATE_CANVAS_ROTATION,
            IRComponents::C_LocalTransform,
            IRComponents::C_RotationMode,
            IRComponents::C_EntityCanvas>("PropagateCanvasRotation");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PROPAGATE_CANVAS_ROTATION_H */
