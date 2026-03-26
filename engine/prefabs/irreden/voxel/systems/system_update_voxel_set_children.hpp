#ifndef SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H
#define SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H

#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/components/component_player.hpp>
#include <irreden/ir_render.hpp>

#include <unordered_map>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {
template <> struct System<UPDATE_VOXEL_SET_CHILDREN> {
    static SystemId create() {
        // Cached per-canvas pool pointers. Only a handful of canvases exist,
        // so this map stays tiny and lookups are negligible.
        // TODO: replace with relation-based archetype grouping (RENDERS_ON)
        // so the pool is resolved once per archetype node, not per entity.
        static std::unordered_map<IREntity::EntityId, C_VoxelPool *> poolCache;

        return createSystem<C_VoxelSetNew, C_PositionGlobal3D>(
            "UpdateVoxelSetChildren",
            [](IREntity::EntityId &entityId,
               C_VoxelSetNew &voxelSet,
               C_PositionGlobal3D &position) {
                IREntity::EntityId canvas = voxelSet.canvasEntity_;
                if (canvas == IREntity::kNullEntity) {
                    canvas = IRRender::getActiveCanvasEntity();
                }

                auto it = poolCache.find(canvas);
                if (it == poolCache.end()) {
                    it = poolCache.emplace(
                        canvas, &IREntity::getComponent<C_VoxelPool>(canvas)
                    ).first;
                }
                C_VoxelPool *pool = it->second;

                voxelSet.updateAsChild(position.pos_);

                if (voxelSet.ownerEntityId_ == IREntity::kNullEntity &&
                    entityId != IREntity::kNullEntity &&
                    voxelSet.numVoxels_ > 0) {
                    voxelSet.ownerEntityId_ = entityId;
                    size_t startIdx = voxelSet.globalPositions_.data() -
                                      pool->getPositionGlobalsBasePtr();
                    pool->setEntityIdForRange(startIdx, voxelSet.numVoxels_, entityId);
                }
            }
        );
    }
};
} // namespace IRSystem

#endif /* SYSTEM_UPDATE_VOXEL_SET_CHILDREN_H */
