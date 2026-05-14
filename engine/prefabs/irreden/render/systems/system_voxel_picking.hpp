#ifndef SYSTEM_VOXEL_PICKING_H
#define SYSTEM_VOXEL_PICKING_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/render/components/component_voxel_selection.hpp>
#include <irreden/render/picking.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

namespace IRSystem {

// Editor voxel-picking driver. Iterates the highlight entity (carries
// C_VoxelSelection + C_VoxelSelectionHighlight + position +
// C_ShapeDescriptor) once per frame. On left-click PRESSED, casts a
// ray through the cursor and writes the hit (or clears selection) to
// C_VoxelSelection. On a successful hit the highlight's position
// components and shape visibility flag are updated so the highlight
// redraws at the picked voxel the same frame.
//
// Registered in the RENDER pipeline *after* the camera systems so the
// raycast reads the post-pan/rotate camera state, and *before*
// VOXEL_TO_TRIXEL_STAGE_1 so the highlight rasterizes at the new
// position without a one-frame lag. The system writes C_Position3D
// and C_PositionGlobal3D together because UPDATE has already run by
// the time RENDER ticks — GLOBAL_POSITION_3D won't re-resolve until
// next frame.
template <> struct System<VOXEL_PICKING> {
    void tick(
        IREntity::EntityId entityId,
        IRComponents::C_VoxelSelection &selection,
        const IRComponents::C_VoxelSelectionHighlight &,
        IRComponents::C_Position3D &pos,
        IRComponents::C_PositionGlobal3D &globalPos,
        IRComponents::C_ShapeDescriptor &highlightShape
    ) {
        const bool clicked =
            IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::PRESSED);
        if (!clicked)
            return;

        const auto hit = IRPrefab::Picking::castVoxelRay(entityId);
        if (hit) {
            selection.hasSelection_ = true;
            selection.voxelPos_ = hit->voxelPos_;
            selection.worldHitPos_ = hit->worldHitPos_;
            selection.hitEntity_ = hit->entity_;

            const IRMath::vec3 worldVoxel = IRMath::vec3(hit->voxelPos_);
            pos.pos_ = worldVoxel;
            // Highlight has no parent — global == local for this entity.
            globalPos.pos_ = worldVoxel;
            highlightShape.flags_ |= IRRender::SHAPE_FLAG_VISIBLE;
        } else {
            selection.hasSelection_ = false;
            selection.hitEntity_ = IREntity::kNullEntity;
            highlightShape.flags_ &= ~IRRender::SHAPE_FLAG_VISIBLE;
        }
    }

    static SystemId create() {
        return registerSystem<
            VOXEL_PICKING,
            IRComponents::C_VoxelSelection,
            IRComponents::C_VoxelSelectionHighlight,
            IRComponents::C_Position3D,
            IRComponents::C_PositionGlobal3D,
            IRComponents::C_ShapeDescriptor>("VoxelPicking");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_VOXEL_PICKING_H */
