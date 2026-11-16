#ifndef IR_PREFAB_VOXEL_OWNER_H
#define IR_PREFAB_VOXEL_OWNER_H

#include <irreden/ir_entity.hpp>

#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/voxel_pool_api.hpp>

#include <cstddef>

namespace IRPrefab::Voxel {

/// Names `entity` as the owner of `set` and registers it over the set's
/// resident span in `pool`, which the GPU picking buffer reads. A staged or
/// empty set only records the id; its span is registered when it is seeded.
inline void adoptOwner(
    IRComponents::C_VoxelSetNew &set, IREntity::EntityId entity, IRComponents::C_VoxelPool &pool
) {
    set.ownerEntityId_ = entity;
    if (set.numVoxels_ > 0) {
        pool.setEntityIdForRange(
            set.voxelStartIdx_,
            static_cast<std::size_t>(set.allocatedVoxels_),
            entity
        );
    }
}

/// `adoptOwner` for a caller that does not hold the pool: resolves it from
/// the set's canvas. Main thread only.
inline void adoptOwner(IRComponents::C_VoxelSetNew &set, IREntity::EntityId entity) {
    set.ownerEntityId_ = entity;
    if (set.numVoxels_ <= 0) {
        return;
    }
    IRPrefab::VoxelPool::withPoolByEntity(set.canvasEntity_, [&](IRComponents::C_VoxelPool &pool) {
        adoptOwner(set, entity, pool);
    });
}

} // namespace IRPrefab::Voxel

#endif /* IR_PREFAB_VOXEL_OWNER_H */
