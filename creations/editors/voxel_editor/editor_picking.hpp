#ifndef IR_VOXEL_EDITOR_PICKING_H
#define IR_VOXEL_EDITOR_PICKING_H

#include <irreden/common/rotation_mode.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/components/component_gizmo_handle.hpp>
#include <irreden/render/picking.hpp>

#include <optional>

// The editor's picking contract:
//   - the nearest EDITABLE voxel under the cursor is what a click edits;
//   - the face it edits through is the one drawn under the cursor;
//   - reference furniture never takes the click — it is passed through, even
//     when it is drawn in front of the editable voxel;
//   - a gizmo handle takes the click only on the pixels it is drawn on.
namespace IRComponents {

// Marks a voxel set as scene furniture: visible, but not something a click
// edits. SDF shapes need no tag — a shape hit carries no face, so the edit
// pick leaves shapes out wholesale.
struct C_EditorReference {};

} // namespace IRComponents

namespace IRVoxelEditor {

inline bool isEditable(IREntity::EntityId entity) {
    if (IREntity::getComponentOptional<IRComponents::C_EditorReference>(entity).has_value()) {
        return false;
    }
    const auto mode = IREntity::getComponentOptional<IRComponents::C_RotationMode>(entity);
    if (mode && IRPrefab::RotationMode::ownsEntityCanvas(mode.value()->mode_)) {
        return false;
    }
    const auto world = IREntity::getComponentOptional<IRComponents::C_WorldTransform>(entity);
    return !world || world.value()->rotation_ == IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f);
}

// What a place / erase gesture acts on this frame.
inline std::optional<IRPrefab::Picking::RayHit> pickEditable() {
    return IRPrefab::Picking::castVoxelRay(
        IRPrefab::Picking::RayCastOptions{
            .cursorRay_ = IRPrefab::Picking::CursorRay::SCREEN_PIXEL,
            .shapes_ = false,
            .accepts_ = &isEditable,
        }
    );
}

// True when the pixel under the cursor shows a gizmo handle, read from the
// same GPU entity-id readback GIZMO_HOVER uses — so a handle owns exactly the
// pixels it is drawn on, and a click beside it reaches the scene.
inline bool cursorOnGizmoHandle() {
    const IREntity::EntityId hovered = IRRender::getEntityIdAtMouseTrixel();
    return hovered != IREntity::kNullEntity &&
           IREntity::getComponentOptional<IRComponents::C_GizmoHandle>(hovered).has_value();
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_PICKING_H */
