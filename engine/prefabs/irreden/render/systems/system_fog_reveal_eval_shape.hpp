#ifndef SYSTEM_FOG_REVEAL_EVAL_SHAPE_H
#define SYSTEM_FOG_REVEAL_EVAL_SHAPE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_reveal_settings.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

#include <cstdint>

namespace IRSystem {

template <> struct System<FOG_REVEAL_EVAL_SHAPE> {
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    IREntity::EntityId activeCanvas_ = IREntity::kNullEntity;
    const IRComponents::C_CanvasFogOfWar *fog_ = nullptr;
    IRComponents::C_FogRevealSettings settings_{};
    std::uint64_t frameCounter_ = 0;

    void beginTick() {
        activeCanvas_ = IRRender::getActiveCanvasEntityOrNull();
        fog_ = nullptr;
        if (activeCanvas_ != IREntity::kNullEntity) {
            if (auto fog =
                    IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(activeCanvas_)) {
                fog_ = *fog;
            }
        }
        settings_ = IREntity::singleton<IRComponents::C_FogRevealSettings>();
        settings_.staggerPeriod_ = IRMath::max(settings_.staggerPeriod_, std::uint32_t{1});
        ++frameCounter_;
    }

    void tick(
        IREntity::EntityId &entity,
        IRComponents::C_FogRevealed &revealed,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_ShapeDescriptor &shape
    ) const {
        if ((entity + frameCounter_) % settings_.staggerPeriod_ != 0u) {
            return;
        }
        if (shape.canvasEntity_ != IREntity::kNullEntity && shape.canvasEntity_ != activeCanvas_) {
            return;
        }

        revealed.revealFactor_ =
            fog_ == nullptr ? 1.0f : IRPrefab::Fog::evalReveal(*fog_, worldTransform.translation_);
        if (!revealed.shown_ && revealed.revealFactor_ >= settings_.showThreshold_) {
            revealed.shown_ = true;
        } else if (revealed.shown_ && revealed.revealFactor_ <= settings_.hideThreshold_) {
            revealed.shown_ = false;
        }

        shape.fogBodyFactor_ = IRPrefab::Fog::quantizeRevealFactor(revealed.revealFactor_);
        shape.flags_ |= IRRender::SHAPE_FLAG_FOG_BODY;
        if (revealed.shown_) {
            shape.flags_ &= ~IRRender::SHAPE_FLAG_FOG_HIDDEN;
        } else {
            shape.flags_ |= IRRender::SHAPE_FLAG_FOG_HIDDEN;
        }
    }

    static SystemId create() {
        return registerSystem<
            FOG_REVEAL_EVAL_SHAPE,
            IRComponents::C_FogRevealed,
            IRComponents::C_WorldTransform,
            IRComponents::C_ShapeDescriptor,
            ParallelSafe>("FogRevealEvalShape");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FOG_REVEAL_EVAL_SHAPE_H */
