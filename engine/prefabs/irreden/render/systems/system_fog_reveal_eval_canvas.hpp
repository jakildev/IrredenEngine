#ifndef SYSTEM_FOG_REVEAL_EVAL_CANVAS_H
#define SYSTEM_FOG_REVEAL_EVAL_CANVAS_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_fog_reveal_settings.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>

#include <cstdint>

namespace IRSystem {

template <> struct System<FOG_REVEAL_EVAL_CANVAS> {
    const IRComponents::C_CanvasFogOfWar *fog_ = nullptr;
    IRComponents::C_FogRevealSettings settings_{};
    std::uint64_t frameCounter_ = 0;
    IREntity::EntityId activeCanvas_ = IREntity::kNullEntity;

    void beginTick() {
        fog_ = nullptr;
        activeCanvas_ = IRRender::getActiveCanvasEntityOrNull();
        if (activeCanvas_ != IREntity::kNullEntity) {
            if (auto attached =
                    IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(activeCanvas_)) {
                fog_ = *attached;
            }
        }
        settings_ = IREntity::singleton<IRComponents::C_FogRevealSettings>();
        settings_.staggerPeriod_ = IRMath::max(settings_.staggerPeriod_, std::uint32_t{1});
        ++frameCounter_;
    }

    void tick(
        IREntity::EntityId entity,
        IRComponents::C_FogRevealed &revealed,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_EntityCanvas &entityCanvas
    ) const {
        if (entity == activeCanvas_ ||
            !IREntity::getComponentOptional<IRComponents::C_DetachedCanvas>(
                entityCanvas.canvasEntity_
            )) {
            return;
        }
        if ((entity + frameCounter_) % settings_.staggerPeriod_ != 0u) {
            return;
        }
        if (fog_ == nullptr || entityCanvas.screenLocked_) {
            revealed.revealFactor_ = 1.0f;
            revealed.shown_ = true;
        } else {
            revealed.revealFactor_ = IRPrefab::Fog::evalReveal(*fog_, worldTransform.translation_);
            if (!revealed.shown_ && revealed.revealFactor_ >= settings_.showThreshold_) {
                revealed.shown_ = true;
            } else if (revealed.shown_ && revealed.revealFactor_ <= settings_.hideThreshold_) {
                revealed.shown_ = false;
            }
        }

        entityCanvas.fogRevealFactor_ = revealed.revealFactor_;
        entityCanvas.fogHidden_ = !revealed.shown_;
    }

    static SystemId create() {
        return registerSystem<
            FOG_REVEAL_EVAL_CANVAS,
            IRComponents::C_FogRevealed,
            IRComponents::C_WorldTransform,
            IRComponents::C_EntityCanvas>("FogRevealEvalCanvas");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FOG_REVEAL_EVAL_CANVAS_H */
