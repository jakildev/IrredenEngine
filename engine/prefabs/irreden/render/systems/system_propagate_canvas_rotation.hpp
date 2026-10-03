#ifndef SYSTEM_PROPAGATE_CANVAS_ROTATION_H
#define SYSTEM_PROPAGATE_CANVAS_ROTATION_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/camera.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/render/canvas_pose.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>

// PROPAGATE_CANVAS_ROTATION — UPDATE pipeline.
//
// Composes the inverse world-camera rotation with each DETACHED entity's
// WORLD rotation and writes the result onto the per-entity canvas as
// `C_CanvasLocalRotation`, so the RENDER pipeline's
// VOXEL_TO_TRIXEL_STAGE_1 bakes the result into the canvas's voxel emit
// via IRMath::faceDeformationMatrixSO3. The world rotation, not the local
// one: a detached child under a rotating parent must turn with the parent,
// exactly as its composited translation already follows the parent. The
// camera composition cancels the camera basis the world canvas already
// applies to the composited per-entity canvas, so a DETACHED entity at
// identity rotation stays stationary in camera-space as the world camera
// spins — matching GRID-mode behavior. The camera surface is just Z-yaw
// today; a full SO(3) camera implementation would flow through
// `IRPrefab::Camera::getRotationQuat()`.
//
// The canvas child carries `C_CanvasLocalRotation` (attached at creation
// by Prefab<kVoxelPoolCanvas>), so the write is an in-place value update
// — no archetype migration — and is safe against the foreign child
// mid-tick. GRID-mode entities are skipped (the camera basis still
// reaches them through the rasterYaw / faceDeform residual path).
//
// Register in the UPDATE pipeline after PROPAGATE_TRANSFORM; it must run
// before PROPAGATE_CANVAS_PARTS and before the RENDER pipeline reads the
// value.

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
        const IRComponents::C_WorldTransform &worldTransform,
        const IRComponents::C_RotationMode &rotationMode,
        const IRComponents::C_EntityCanvas &entityCanvas
    ) {
        // GRID is skipped (the camera basis reaches it through the rasterYaw /
        // faceDeform residual path); both detached strategies need the
        // camera-composed pose on the canvas.
        if (rotationMode.mode_ != IRComponents::RotationMode::DETACHED &&
            rotationMode.mode_ != IRComponents::RotationMode::DETACHED_REVOXELIZE) {
            return;
        }
        auto canvasRotation = IREntity::getComponentOptional<IRComponents::C_CanvasLocalRotation>(
            entityCanvas.canvasEntity_
        );
        if (canvasRotation.has_value()) {
            IRPrefab::CanvasPose::write(
                *canvasRotation.value(),
                cameraRotationInverse_,
                worldTransform,
                rotationMode.mode_,
                entityCanvas
            );
        }
    }

    static SystemId create() {
        return registerSystem<
            PROPAGATE_CANVAS_ROTATION,
            IRComponents::C_WorldTransform,
            IRComponents::C_RotationMode,
            IRComponents::C_EntityCanvas>("PropagateCanvasRotation");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PROPAGATE_CANVAS_ROTATION_H */
