#ifndef SYSTEM_PROPAGATE_CANVAS_ROTATION_H
#define SYSTEM_PROPAGATE_CANVAS_ROTATION_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>

// PROPAGATE_CANVAS_ROTATION (T-295, Epic C C7) — UPDATE pipeline.
//
// Copies each DETACHED entity's `C_LocalTransform` rotation onto its
// per-entity canvas child as `C_CanvasLocalRotation`, so the RENDER pipeline's
// VOXEL_TO_TRIXEL_STAGE_1 can bake full SO(3) rotation into the canvas's voxel
// emit (via IRMath::faceDeformationMatrixSO3) instead of the camera-residual
// deformation. The canvas child already carries `C_CanvasLocalRotation`
// (attached at creation by Prefab<kVoxelPoolCanvas>), so the write is an
// in-place value update — no archetype migration — and is safe against the
// foreign child mid-tick. GRID-mode entities are skipped.
//
// Register in the UPDATE pipeline after PROPAGATE_TRANSFORM; it must run
// before the RENDER pipeline reads the value.

namespace IRSystem {

template <> struct System<PROPAGATE_CANVAS_ROTATION> {
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
            canvasRotation.value()->rotation_ = localTransform.rotation_;
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
