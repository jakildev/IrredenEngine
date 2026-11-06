#ifndef IR_VOXEL_EDITOR_PICKING_H
#define IR_VOXEL_EDITOR_PICKING_H

#include <irreden/common/rotation_mode.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/components/component_gizmo_handle.hpp>
#include <irreden/render/picking.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>

#include <optional>

#include "sculpt_grid.hpp"

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

// Marks a gizmo handle the editor drives itself: a recipe handle, which
// GIZMO_DRAG never drags. index_ is the handle's place in its recipe's list.
// The gizmo-handle census leaves these out.
struct C_EditorRecipeHandle {
    int index_ = 0;
};

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
    // Current cell authoring uses an axis-aligned, unit-spaced world grid.
    // This checks composed world scale, including scale inherited from parents.
    return !world || (world.value()->rotation_ == IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f) &&
                      world.value()->scale_ == IRMath::vec3(1.0f));
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

inline std::optional<IRMath::ivec3>
pickEditablePlane(IREntity::EntityId entity, int axis, int slice, float planeOffset = 0.0f) {
    if (entity == IREntity::kNullEntity || !isEditable(entity))
        return std::nullopt;
    const auto set = IREntity::getComponentOptional<IRComponents::C_VoxelSetNew>(entity);
    if (!set || set.value()->globalPositions_.empty())
        return std::nullopt;
    const IRMath::vec3 rayOrigin = IRRender::mouseWorldPos3DAtIsoDepthExact(0.0f);
    const IRMath::vec3 direction = IRRender::mouseWorldPos3DAtIsoDepthExact(3.0f) - rayOrigin;
    const IRMath::vec3 origin =
        IRMath::vec3(IRMath::roundVec3HalfUp(set.value()->globalPositions_[0].pos_));
    return pickSculptPlane(
        rayOrigin - origin,
        direction,
        set.value()->size_,
        axis,
        slice,
        planeOffset
    );
}

namespace detail {

struct VoxelColorHit {
    IRMath::Color color_;
    float rayT_;
};

inline std::optional<VoxelColorHit> colorFromDenseGrid(
    IRMath::vec3 rayOrigin,
    IRMath::vec3 rayDirection,
    IRMath::vec3 gridOrigin,
    IRMath::ivec3 size,
    std::span<const IRComponents::C_Voxel> records
) {
    const auto hit = IRPrefab::Picking::castGridRay(
        rayOrigin - gridOrigin,
        rayDirection,
        size,
        [&](IRMath::ivec3 cell) {
            const auto index = static_cast<std::size_t>(IRMath::index3DtoIndex1D(cell, size));
            return index < records.size() && records[index].color_.alpha_ != 0;
        }
    );
    if (!hit)
        return std::nullopt;
    return VoxelColorHit{records[IRMath::index3DtoIndex1D(hit->cell_, size)].color_, hit->rayT_};
}

inline bool colorSampleAllowed(IREntity::EntityId entity) {
    if (IREntity::getEntityManager().isMarkedForDeletion(entity) ||
        IREntity::getComponentOptional<IRComponents::C_GizmoHandle>(entity) ||
        IREntity::getComponentOptional<IRComponents::C_EditorRecipeHandle>(entity))
        return false;
    if (IREntity::getComponentOptional<IRComponents::C_EntityCanvas>(entity))
        return false;
    if (const auto set = IREntity::getComponentOptional<IRComponents::C_VoxelSetNew>(entity))
        if (!(*set)->renders() || (*set)->gpuTransformSlot_ != IRRender::kVoxelTransformStatic)
            return false;
    if (const auto mode = IREntity::getComponentOptional<IRComponents::C_RotationMode>(entity))
        if (IRPrefab::RotationMode::ownsEntityCanvas((*mode)->mode_))
            return false;
    return true;
}

} // namespace detail

// Reads unlit voxel records. Private-canvas composites and GPU-skinned geometry
// require a displayed source-cell mapping; their local records are not world cells.
inline std::optional<IRMath::Color> pickSceneVoxelColor() {
    const auto hovered = IRRender::getEntityIdAtMouseTrixel();
    if (hovered != IREntity::kNullEntity && IREntity::entityExists(hovered) &&
        (!detail::colorSampleAllowed(hovered) ||
         !IREntity::getComponentOptional<IRComponents::C_VoxelSetNew>(hovered)))
        return std::nullopt;
    const auto origin = IRRender::mouseWorldPos3DAtIsoDepthExact(0);
    const auto direction = IRRender::mouseWorldPos3DAtIsoDepthExact(3) - origin;
    std::optional<detail::VoxelColorHit> nearest;
    const auto keepNearest = [&](const std::optional<detail::VoxelColorHit> &hit) {
        if (hit && (!nearest || hit->rayT_ < nearest->rayT_))
            nearest = hit;
    };
    IREntity::forEachComponent<IRComponents::C_VoxelSetNew>(
        [&](IREntity::EntityId entity, const IRComponents::C_VoxelSetNew &set) {
            if (!set.renders() || !detail::colorSampleAllowed(entity) ||
                set.gpuTransformSlot_ != IRRender::kVoxelTransformStatic ||
                set.globalPositions_.empty() || set.voxels_.empty())
                return;
            const auto world =
                IREntity::getComponentOptional<IRComponents::C_WorldTransform>(entity);
            const bool dense = set.rotationSourceVoxels_.empty() && world &&
                               (*world)->rotation_ == IRMath::vec4(0, 0, 0, 1) &&
                               (*world)->scale_ == IRMath::vec3(1);
            if (dense) {
                keepNearest(
                    detail::colorFromDenseGrid(
                        origin,
                        direction,
                        set.globalPositions_[0].pos_,
                        set.size_,
                        set.voxels_
                    )
                );
                return;
            }
            IRPrefab::VoxelPool::withPoolByEntity(
                set.canvasEntity_,
                [&](const IRComponents::C_VoxelPool &pool) {
                    const auto &positions = pool.getPositionGlobals();
                    const auto &records = pool.getColors();
                    const auto &mask = pool.getActiveMask();
                    const auto end = IRMath::min(
                        set.voxelStartIdx_ + static_cast<std::size_t>(set.allocatedVoxels_),
                        records.size()
                    );
                    for (auto index = set.voxelStartIdx_; index < end; ++index) {
                        if (index >= positions.size() || index / 32 >= mask.size() ||
                            (mask[index / 32] & (1u << (index % 32))) == 0 ||
                            records[index].color_.alpha_ == 0)
                            continue;
                        keepNearest(
                            detail::colorFromDenseGrid(
                                origin,
                                direction,
                                positions[index].pos_,
                                IRMath::ivec3(1),
                                std::span<const IRComponents::C_Voxel>(&records[index], 1)
                            )
                        );
                    }
                }
            );
        }
    );
    if (!nearest)
        return std::nullopt;
    // Visible SDFs can occlude a voxel even though they provide no voxel color.
    const auto shape = IRPrefab::Picking::castVoxelRay(
        IRPrefab::Picking::RayCastOptions{
            .cursorRay_ = IRPrefab::Picking::CursorRay::SCREEN_PIXEL,
            .accepts_ = +[](IREntity::EntityId entity) {
                return !IREntity::getComponentOptional<IRComponents::C_VoxelSetNew>(entity)
                            .has_value();
            }
        }
    );
    if (shape && IRMath::dot(shape->worldHitPos_ - origin, direction) <
                     nearest->rayT_ * IRMath::dot(direction, direction))
        return std::nullopt;
    return nearest->color_;
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
