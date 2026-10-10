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
#include <irreden/render/components/component_fog_ghost.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace IRSystem {

namespace detail {

template <bool kGhost> struct FogRevealEvalCanvas {
    const IRComponents::C_CanvasFogOfWar *fog_ = nullptr;
    IRComponents::C_FogRevealSettings settings_{};
    std::uint64_t frameCounter_ = 0;
    IREntity::EntityId activeCanvas_ = IREntity::kNullEntity;
    struct HeldPose {
        IREntity::EntityId entity_ = IREntity::kNullEntity;
        IRComponents::C_WorldTransform pose_{};
    };
    std::vector<HeldPose> heldGhostPoses_;

    void beginTick() {
        if constexpr (kGhost) {
            heldGhostPoses_.clear();
            std::size_t population = 0;
            for (IREntity::ArchetypeNode *node : matchingNodes()) {
                population += static_cast<std::size_t>(node->length_);
            }
            heldGhostPoses_.reserve(population);
        }
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

    static std::vector<IREntity::ArchetypeNode *> matchingNodes() {
        if constexpr (kGhost) {
            return IREntity::queryArchetypeNodesSimple(
                IREntity::getArchetype<
                    IRComponents::C_FogRevealed,
                    IRComponents::C_FogGhost,
                    IRComponents::C_WorldTransform,
                    IRComponents::C_EntityCanvas>()
            );
        }
        return IREntity::queryArchetypeNodesSimple(
            IREntity::getArchetype<
                IRComponents::C_FogRevealed,
                IRComponents::C_WorldTransform,
                IRComponents::C_EntityCanvas>(),
            IREntity::getArchetype<IRComponents::C_FogGhost>()
        );
    }

    void tickImpl(
        IREntity::EntityId entity,
        IRComponents::C_FogRevealed &revealed,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_EntityCanvas &entityCanvas,
        IRComponents::C_FogGhost *ghost
    ) {
        if (entity == activeCanvas_ ||
            !IREntity::getComponentOptional<IRComponents::C_DetachedCanvas>(
                entityCanvas.canvasEntity_
            )) {
            return;
        }
        const bool wasShown = revealed.shown_;
        const bool evaluated = revealed.override_ != IRComponents::FogOverride::NONE ||
                               (entity + frameCounter_) % settings_.staggerPeriod_ == 0u;
        if constexpr (!kGhost) {
            if (!evaluated) {
                return;
            }
        }
        if (evaluated && revealed.override_ == IRComponents::FogOverride::FORCE_REVEALED) {
            revealed.revealFactor_ = 1.0f;
            revealed.shown_ = true;
        } else if (evaluated && revealed.override_ == IRComponents::FogOverride::FORCE_HIDDEN) {
            revealed.revealFactor_ = 0.0f;
            revealed.shown_ = false;
        } else if (evaluated && (fog_ == nullptr || entityCanvas.screenLocked_)) {
            revealed.revealFactor_ = 1.0f;
            revealed.shown_ = true;
        } else if (evaluated) {
            revealed.revealFactor_ =
                IRPrefab::Fog::evalReveal(*fog_, worldTransform.translation_, revealed.channels_);
            if (!revealed.shown_ && revealed.revealFactor_ >= settings_.showThreshold_) {
                revealed.shown_ = true;
            } else if (revealed.shown_ && revealed.revealFactor_ <= settings_.hideThreshold_) {
                revealed.shown_ = false;
            }
        }

        if constexpr (kGhost) {
            IRPrefab::Fog::stepGhostLifecycle(
                revealed,
                *ghost,
                worldTransform,
                wasShown,
                evaluated,
                settings_.showThreshold_,
                [this](IRMath::vec3 position, std::uint32_t channels) {
                    return fog_ == nullptr ? 1.0f
                                           : IRPrefab::Fog::evalReveal(*fog_, position, channels);
                }
            );
        }

        if (revealed.ghostHeld_) {
            heldGhostPoses_.push_back(HeldPose{entity, ghost->pose_});
        }
        entityCanvas.fogRevealFactor_ =
            revealed.ghostHeld_ ? static_cast<float>(IRComponents::kFogStateExplored) / 255.0f
                                : revealed.revealFactor_;
        entityCanvas.fogHidden_ = !revealed.shown_ && !revealed.ghostHeld_;
        entityCanvas.fogGhost_ = revealed.ghostHeld_;
    }

    void tick(
        IREntity::EntityId entity,
        IRComponents::C_FogRevealed &revealed,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_EntityCanvas &entityCanvas
    ) {
        if (entity == activeCanvas_ ||
            !IREntity::getComponentOptional<IRComponents::C_DetachedCanvas>(
                entityCanvas.canvasEntity_
            )) {
            return;
        }
        if (revealed.override_ == IRComponents::FogOverride::NONE &&
            (entity + frameCounter_) % settings_.staggerPeriod_ != 0u) {
            return;
        }
        if (revealed.override_ == IRComponents::FogOverride::FORCE_REVEALED) {
            revealed.revealFactor_ = 1.0f;
            revealed.shown_ = true;
        } else if (revealed.override_ == IRComponents::FogOverride::FORCE_HIDDEN) {
            revealed.revealFactor_ = 0.0f;
            revealed.shown_ = false;
        } else if (fog_ == nullptr || entityCanvas.screenLocked_) {
            revealed.revealFactor_ = 1.0f;
            revealed.shown_ = true;
        } else {
            revealed.revealFactor_ =
                IRPrefab::Fog::evalReveal(*fog_, worldTransform.translation_, revealed.channels_);
            if (!revealed.shown_ && revealed.revealFactor_ >= settings_.showThreshold_) {
                revealed.shown_ = true;
            } else if (revealed.shown_ && revealed.revealFactor_ <= settings_.hideThreshold_) {
                revealed.shown_ = false;
            }
        }
        entityCanvas.fogRevealFactor_ = revealed.revealFactor_;
        entityCanvas.fogHidden_ = !revealed.shown_;
    }

    void endTick() {
        if constexpr (!kGhost) {
            return;
        }
        std::sort(
            heldGhostPoses_.begin(),
            heldGhostPoses_.end(),
            [](const HeldPose &a, const HeldPose &b) { return a.entity_ < b.entity_; }
        );
    }

    void tickGhost(
        IREntity::EntityId entity,
        IRComponents::C_FogRevealed &revealed,
        IRComponents::C_FogGhost &ghost,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_EntityCanvas &entityCanvas
    ) {
        tickImpl(entity, revealed, worldTransform, entityCanvas, &ghost);
    }
};

} // namespace detail

template <> struct System<FOG_REVEAL_EVAL_CANVAS> : detail::FogRevealEvalCanvas<false> {
    static SystemId create() {
        return registerSystem<
            FOG_REVEAL_EVAL_CANVAS,
            IRComponents::C_FogRevealed,
            IRComponents::C_WorldTransform,
            IRComponents::C_EntityCanvas,
            Exclude<IRComponents::C_FogGhost>>("FogRevealEvalCanvas");
    }
};

template <> struct System<FOG_REVEAL_EVAL_CANVAS_GHOST> : detail::FogRevealEvalCanvas<true> {
    void tick(
        IREntity::EntityId entity,
        IRComponents::C_FogRevealed &revealed,
        IRComponents::C_FogGhost &ghost,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_EntityCanvas &entityCanvas
    ) {
        tickGhost(entity, revealed, ghost, worldTransform, entityCanvas);
    }

    static SystemId create() {
        return registerSystem<
            FOG_REVEAL_EVAL_CANVAS_GHOST,
            IRComponents::C_FogRevealed,
            IRComponents::C_FogGhost,
            IRComponents::C_WorldTransform,
            IRComponents::C_EntityCanvas>("FogRevealEvalCanvasGhost");
    }
};

} // namespace IRSystem

namespace IRPrefab::Fog {
inline const auto &heldCanvasGhostPoses() {
    using Eval = IRSystem::System<IRSystem::FOG_REVEAL_EVAL_CANVAS_GHOST>;
    static const std::vector<Eval::HeldPose> empty;
    const auto id = IRSystem::findSystem(IRSystem::FOG_REVEAL_EVAL_CANVAS_GHOST);
    return id == IRSystem::kNullSystemId ? empty
                                         : IRSystem::getSystemParams<Eval>(id)->heldGhostPoses_;
}
} // namespace IRPrefab::Fog

#endif /* SYSTEM_FOG_REVEAL_EVAL_CANVAS_H */
