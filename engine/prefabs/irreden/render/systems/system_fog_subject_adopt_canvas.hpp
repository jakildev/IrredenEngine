#ifndef SYSTEM_FOG_SUBJECT_ADOPT_CANVAS_H
#define SYSTEM_FOG_SUBJECT_ADOPT_CANVAS_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_fog_exempt.hpp>
#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/render/components/component_fog_reveal_settings.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>

namespace IRSystem {

template <> struct System<FOG_SUBJECT_ADOPT_CANVAS> {
    const IRComponents::C_CanvasFogOfWar *fog_ = nullptr;
    IRComponents::C_FogRevealSettings settings_{};
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
    }

    void tick(
        IREntity::EntityId entity,
        IRComponents::C_EntityCanvas &entityCanvas,
        const IRComponents::C_WorldTransform &worldTransform
    ) const {
        if (fog_ == nullptr || entity == activeCanvas_ || entityCanvas.screenLocked_ ||
            !IREntity::getComponentOptional<IRComponents::C_DetachedCanvas>(
                entityCanvas.canvasEntity_
            )) {
            return;
        }

        IRComponents::C_FogRevealed revealed{};
        revealed.revealFactor_ = IRPrefab::Fog::evalReveal(*fog_, worldTransform.translation_);
        revealed.shown_ = revealed.revealFactor_ >= settings_.showThreshold_;
        entityCanvas.fogRevealFactor_ = revealed.revealFactor_;
        entityCanvas.fogHidden_ = !revealed.shown_;
        IRPrefab::Fog::stampCanvasBodyCarrier(
            entityCanvas,
            true,
            IRPrefab::Fog::quantizeRevealFactor(revealed.revealFactor_)
        );
        IREntity::setComponentDeferred(entity, revealed);
    }

    static SystemId create() {
        return registerSystem<
            FOG_SUBJECT_ADOPT_CANVAS,
            IRComponents::C_EntityCanvas,
            IRComponents::C_WorldTransform,
            Exclude<IRComponents::C_FogRevealed>,
            Exclude<IRComponents::C_FogField>,
            Exclude<IRComponents::C_FogExempt>>("FogSubjectAdoptCanvas");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FOG_SUBJECT_ADOPT_CANVAS_H */
