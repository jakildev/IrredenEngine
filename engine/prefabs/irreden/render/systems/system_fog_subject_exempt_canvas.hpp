#ifndef SYSTEM_FOG_SUBJECT_EXEMPT_CANVAS_H
#define SYSTEM_FOG_SUBJECT_EXEMPT_CANVAS_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_fog_exempt.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <algorithm>
#include <vector>

namespace IRSystem {

template <> struct System<FOG_SUBJECT_EXEMPT_CANVAS> {
    struct DetachedPool {
        IREntity::EntityId entity_ = IREntity::kNullEntity;
        IRComponents::C_VoxelPool *pool_ = nullptr;
    };

    IREntity::EntityId activeCanvas_ = IREntity::kNullEntity;
    bool enabled_ = false;
    std::vector<DetachedPool> detachedPools_;

    void beginTick() {
        activeCanvas_ = IRRender::getActiveCanvasEntityOrNull();
        enabled_ = activeCanvas_ != IREntity::kNullEntity &&
                   IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(activeCanvas_)
                       .has_value();
        detachedPools_.clear();
        if (!enabled_) {
            return;
        }
        const auto nodes = IREntity::queryArchetypeNodesSimple(
            IREntity::getArchetype<IRComponents::C_VoxelPool, IRComponents::C_DetachedCanvas>()
        );
        for (IREntity::ArchetypeNode *node : nodes) {
            auto &pools = IREntity::getComponentData<IRComponents::C_VoxelPool>(node);
            for (int i = 0; i < node->length_; ++i) {
                detachedPools_.push_back(DetachedPool{node->entities_[i], &pools[i]});
            }
        }
        std::sort(
            detachedPools_.begin(),
            detachedPools_.end(),
            [](const DetachedPool &a, const DetachedPool &b) { return a.entity_ < b.entity_; }
        );
    }

    IRComponents::C_VoxelPool *findPool(IREntity::EntityId entity) const {
        const auto found = std::lower_bound(
            detachedPools_.begin(),
            detachedPools_.end(),
            entity,
            [](const DetachedPool &pool, IREntity::EntityId id) { return pool.entity_ < id; }
        );
        return found != detachedPools_.end() && found->entity_ == entity ? found->pool_ : nullptr;
    }

    void tick(
        IREntity::EntityId entity,
        IRComponents::C_EntityCanvas &entityCanvas,
        const IRComponents::C_FogExempt &
    ) const {
        if (!enabled_ || entity == activeCanvas_ || entityCanvas.screenLocked_) {
            return;
        }
        IRComponents::C_VoxelPool *pool = findPool(entityCanvas.canvasEntity_);
        if (pool == nullptr) {
            return;
        }
        pool->setFogCarrierPolicy(IRComponents::C_VoxelPool::FogCarrierPolicy::BODY, 255);
        entityCanvas.fogRevealFactor_ = 1.0f;
        entityCanvas.fogHidden_ = false;
    }

    static SystemId create() {
        return registerSystem<
            FOG_SUBJECT_EXEMPT_CANVAS,
            IRComponents::C_EntityCanvas,
            IRComponents::C_FogExempt,
            Exclude<IRComponents::C_FogRevealed>>("FogSubjectExemptCanvas");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FOG_SUBJECT_EXEMPT_CANVAS_H */
