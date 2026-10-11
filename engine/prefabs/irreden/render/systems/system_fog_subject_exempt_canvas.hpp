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
#include <irreden/render/detached_canvas_pool_cache.hpp>

namespace IRSystem {

template <> struct System<FOG_SUBJECT_EXEMPT_CANVAS> : IRPrefab::detail::DetachedCanvasPoolCache {
    IREntity::EntityId activeCanvas_ = IREntity::kNullEntity;
    bool enabled_ = false;

    void beginTick() {
        activeCanvas_ = IRRender::getActiveCanvasEntityOrNull();
        enabled_ = activeCanvas_ != IREntity::kNullEntity &&
                   IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(activeCanvas_)
                       .has_value();
        detachedPools_.clear();
        if (!enabled_) {
            return;
        }
        collectDetachedPools();
    }

    void tick(
        IREntity::EntityId entity,
        IRComponents::C_EntityCanvas &entityCanvas,
        const IRComponents::C_FogExempt &
    ) const {
        if (!enabled_ || entity == activeCanvas_ || entityCanvas.screenLocked_) {
            return;
        }
        IRComponents::C_VoxelPool *pool = findDetachedPool(entityCanvas.canvasEntity_);
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
