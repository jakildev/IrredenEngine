#ifndef DETACHED_CANVAS_POOL_CACHE_H
#define DETACHED_CANVAS_POOL_CACHE_H

#include <irreden/ir_entity.hpp>
#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <algorithm>
#include <vector>

namespace IRPrefab::detail {

struct DetachedCanvasPoolCache {
    struct Entry {
        IREntity::EntityId entity_ = IREntity::kNullEntity;
        IRComponents::C_VoxelPool *pool_ = nullptr;
    };

    std::vector<Entry> detachedPools_;

    void collectDetachedPools() {
        detachedPools_.clear();
        const auto nodes = IREntity::queryArchetypeNodesSimple(
            IREntity::getArchetype<IRComponents::C_VoxelPool, IRComponents::C_DetachedCanvas>()
        );
        for (IREntity::ArchetypeNode *node : nodes) {
            auto &pools = IREntity::getComponentData<IRComponents::C_VoxelPool>(node);
            for (int i = 0; i < node->length_; ++i) {
                detachedPools_.push_back(Entry{node->entities_[i], &pools[i]});
            }
        }
        std::sort(detachedPools_.begin(), detachedPools_.end(), [](const Entry &a, const Entry &b) {
            return a.entity_ < b.entity_;
        });
    }

    IRComponents::C_VoxelPool *findDetachedPool(IREntity::EntityId entity) const {
        const auto found = std::lower_bound(
            detachedPools_.begin(),
            detachedPools_.end(),
            entity,
            [](const Entry &pool, IREntity::EntityId id) { return pool.entity_ < id; }
        );
        return found != detachedPools_.end() && found->entity_ == entity ? found->pool_ : nullptr;
    }
};

} // namespace IRPrefab::detail

#endif /* DETACHED_CANVAS_POOL_CACHE_H */
