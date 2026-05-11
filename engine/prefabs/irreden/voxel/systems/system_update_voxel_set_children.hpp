#ifndef SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H
#define SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H

#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/components/component_player.hpp>
#include <irreden/ir_render.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {
template <> struct System<UPDATE_VOXEL_SET_CHILDREN> {
    struct Params {
        // Per-tick scratch: last-resolved canvas → pool pointer. The pool
        // is fetched fresh each tick (not across frames) because a between-
        // frame canvas archetype migration would invalidate the pointer;
        // within a single tick the archetype is stable so amortizing the
        // lookup is safe. Almost every creation has a single voxel-pool
        // canvas, so in practice this is a one-shot getComponent per tick.
        IREntity::EntityId lastCanvas_ = IREntity::kNullEntity;
        C_VoxelPool *lastPool_ = nullptr;
    };

    static SystemId create() {
        auto paramsOwner = std::make_unique<Params>();
        Params *p = paramsOwner.get();
        SystemId systemId = createSystem<C_VoxelSetNew, C_PositionGlobal3D>(
            "UpdateVoxelSetChildren",
            [p](IREntity::EntityId &entityId,
                C_VoxelSetNew &voxelSet,
                C_PositionGlobal3D &position) {
                // Resolve the voxel set's owning canvas pool through
                // `voxelStartIdx_` rather than through C_VoxelSetNew's
                // captured span members. The spans were taken at allocation
                // time and dangle once the canvas archetype migrates
                // (deep-copy of C_VoxelPool frees the old storage). Writing
                // globals straight into the live pool's
                // `m_voxelPositionsGlobal` keeps it in sync regardless of
                // how creations order their canvas setComponent calls
                // relative to voxel-set allocation.
                IREntity::EntityId canvas = voxelSet.canvasEntity_;
                if (canvas == IREntity::kNullEntity) {
                    canvas = IRRender::getActiveCanvasEntity();
                }
                if (canvas != p->lastCanvas_ || p->lastPool_ == nullptr) {
                    p->lastPool_ = &IREntity::getComponent<C_VoxelPool>(canvas);
                    p->lastCanvas_ = canvas;
                }
                C_VoxelPool &pool = *p->lastPool_;
                voxelSet.updateAsChild(position.pos_, pool.getPositionGlobals());

                if (voxelSet.ownerEntityId_ == IREntity::kNullEntity &&
                    entityId != IREntity::kNullEntity && voxelSet.numVoxels_ > 0) {
                    voxelSet.ownerEntityId_ = entityId;
                    pool.setEntityIdForRange(
                        voxelSet.voxelStartIdx_,
                        static_cast<size_t>(voxelSet.numVoxels_),
                        entityId
                    );
                }
            },
            // beginTick: drop the cached pool pointer at the start of each
            // pipeline run so a between-frame canvas archetype migration
            // can't strand a stale pointer for one frame.
            [p]() {
                p->lastCanvas_ = IREntity::kNullEntity;
                p->lastPool_ = nullptr;
            }
        );
        setSystemParams(systemId, std::move(paramsOwner));
        return systemId;
    }
};
} // namespace IRSystem

#endif /* SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H */
