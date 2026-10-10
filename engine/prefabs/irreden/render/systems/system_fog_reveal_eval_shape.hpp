#ifndef SYSTEM_FOG_REVEAL_EVAL_SHAPE_H
#define SYSTEM_FOG_REVEAL_EVAL_SHAPE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_reveal_settings.hpp>
#include <irreden/render/components/component_fog_ghost.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/job/worker_block_queue.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace IRSystem {

namespace detail {

template <bool kGhost> struct FogRevealEvalShape {
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    IREntity::EntityId activeCanvas_ = IREntity::kNullEntity;
    const IRComponents::C_CanvasFogOfWar *fog_ = nullptr;
    IRComponents::FrameDataFogObservers observers_{};
    IRComponents::FogLosColumnField los_{};
    IRPrefab::Fog::LosHardRouteCache losRoutes_;
    IRComponents::C_FogRevealSettings settings_{};
    std::uint64_t frameCounter_ = 0;
    struct HeldPose {
        IREntity::EntityId entity_ = IREntity::kNullEntity;
        IRComponents::C_WorldTransform pose_{};
    };
    IRJob::WorkerBlockQueue<HeldPose> pendingHeld_;
    std::vector<HeldPose> heldGhostPoses_;

    void beginTick() {
        activeCanvas_ = IRRender::getActiveCanvasEntityOrNull();
        fog_ = nullptr;
        observers_ = {};
        los_ = {};
        IRComponents::C_CanvasFogOfWar *fog = nullptr;
        if (activeCanvas_ != IREntity::kNullEntity) {
            if (auto attached =
                    IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(activeCanvas_)) {
                fog = *attached;
                fog_ = fog;
                IRPrefab::Fog::selectRevealSnapshot(
                    fog_->observers_,
                    fog_->losPublishedObservers_,
                    fog_->losField(),
                    observers_,
                    los_
                );
            }
        }
        losRoutes_.begin(observers_, los_);
        settings_ = IREntity::singleton<IRComponents::C_FogRevealSettings>();
        settings_.staggerPeriod_ = IRMath::max(settings_.staggerPeriod_, std::uint32_t{1});
        ++frameCounter_;
        const std::vector<IREntity::ArchetypeNode *> nodes = matchingNodes();
        if (fog != nullptr) {
            IRPrefab::Fog::touchShapeAnchorRegions(*fog, activeCanvas_, nodes);
        }
        if constexpr (kGhost) {
            std::size_t population = 0;
            for (IREntity::ArchetypeNode *node : nodes) {
                population += static_cast<std::size_t>(node->length_);
            }
            pendingHeld_.reset(population);
        }
    }

    static std::vector<IREntity::ArchetypeNode *> matchingNodes() {
        if constexpr (kGhost) {
            return IREntity::queryArchetypeNodesSimple(
                IREntity::getArchetype<
                    IRComponents::C_FogRevealed,
                    IRComponents::C_FogGhost,
                    IRComponents::C_WorldTransform,
                    IRComponents::C_ShapeDescriptor>()
            );
        }
        return IREntity::queryArchetypeNodesSimple(
            IREntity::getArchetype<
                IRComponents::C_FogRevealed,
                IRComponents::C_WorldTransform,
                IRComponents::C_ShapeDescriptor>(),
            IREntity::getArchetype<IRComponents::C_FogGhost>()
        );
    }

    void tickImpl(
        IREntity::EntityId &entity,
        IRComponents::C_FogRevealed &revealed,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_ShapeDescriptor &shape,
        IRComponents::C_FogGhost *ghost
    ) {
        if (!IRPrefab::Fog::isOnFogCanvas(shape, activeCanvas_)) {
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
        } else if (evaluated) {
            revealed.revealFactor_ = fog_ == nullptr ? 1.0f
                                                     : IRPrefab::Fog::evalReveal(
                                                           *fog_,
                                                           observers_,
                                                           los_,
                                                           losRoutes_,
                                                           worldTransform.translation_,
                                                           revealed.channels_
                                                       );
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
                                           : IRPrefab::Fog::evalReveal(
                                                 *fog_,
                                                 observers_,
                                                 los_,
                                                 losRoutes_,
                                                 position,
                                                 channels
                                             );
                }
            );
        }

        if (revealed.ghostHeld_) {
            pendingHeld_.push(HeldPose{entity, ghost->pose_});
        }
        shape.fogBodyFactor_ = revealed.ghostHeld_
                                   ? IRComponents::kFogStateExplored
                                   : IRPrefab::Fog::quantizeRevealFactor(revealed.revealFactor_);
        shape.flags_ |= IRRender::SHAPE_FLAG_FOG_BODY;
        if (revealed.shown_ || revealed.ghostHeld_) {
            shape.flags_ &= ~IRRender::SHAPE_FLAG_FOG_HIDDEN;
        } else {
            shape.flags_ |= IRRender::SHAPE_FLAG_FOG_HIDDEN;
        }
        if (revealed.ghostHeld_) {
            shape.flags_ |= IRMath::SDF::SHAPE_FLAG_FOG_GHOST;
        } else {
            shape.flags_ &= ~IRMath::SDF::SHAPE_FLAG_FOG_GHOST;
        }
    }

    void tick(
        IREntity::EntityId &entity,
        IRComponents::C_FogRevealed &revealed,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_ShapeDescriptor &shape
    ) {
        if (!IRPrefab::Fog::isOnFogCanvas(shape, activeCanvas_)) {
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
        } else {
            revealed.revealFactor_ = fog_ == nullptr ? 1.0f
                                                     : IRPrefab::Fog::evalReveal(
                                                           *fog_,
                                                           observers_,
                                                           los_,
                                                           losRoutes_,
                                                           worldTransform.translation_,
                                                           revealed.channels_
                                                       );
            if (!revealed.shown_ && revealed.revealFactor_ >= settings_.showThreshold_) {
                revealed.shown_ = true;
            } else if (revealed.shown_ && revealed.revealFactor_ <= settings_.hideThreshold_) {
                revealed.shown_ = false;
            }
        }
        shape.fogBodyFactor_ = IRPrefab::Fog::quantizeRevealFactor(revealed.revealFactor_);
        shape.flags_ |= IRRender::SHAPE_FLAG_FOG_BODY;
        if (revealed.shown_) {
            shape.flags_ &= ~IRRender::SHAPE_FLAG_FOG_HIDDEN;
        } else {
            shape.flags_ |= IRRender::SHAPE_FLAG_FOG_HIDDEN;
        }
    }

    void endTick() {
        if constexpr (!kGhost) {
            return;
        }
        heldGhostPoses_.clear();
        heldGhostPoses_.reserve(pendingHeld_.size());
        pendingHeld_.forEach([this](const HeldPose &entry) { heldGhostPoses_.push_back(entry); });
        std::sort(
            heldGhostPoses_.begin(),
            heldGhostPoses_.end(),
            [](const HeldPose &a, const HeldPose &b) { return a.entity_ < b.entity_; }
        );
    }

    void tickGhost(
        IREntity::EntityId &entity,
        IRComponents::C_FogRevealed &revealed,
        IRComponents::C_FogGhost &ghost,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_ShapeDescriptor &shape
    ) {
        tickImpl(entity, revealed, worldTransform, shape, &ghost);
    }
};

} // namespace detail

template <> struct System<FOG_REVEAL_EVAL_SHAPE> : detail::FogRevealEvalShape<false> {
    static SystemId create() {
        return registerSystem<
            FOG_REVEAL_EVAL_SHAPE,
            IRComponents::C_FogRevealed,
            IRComponents::C_WorldTransform,
            IRComponents::C_ShapeDescriptor,
            Exclude<IRComponents::C_FogGhost>,
            ParallelSafe>("FogRevealEvalShape");
    }
};

template <> struct System<FOG_REVEAL_EVAL_SHAPE_GHOST> : detail::FogRevealEvalShape<true> {
    void tick(
        IREntity::EntityId &entity,
        IRComponents::C_FogRevealed &revealed,
        IRComponents::C_FogGhost &ghost,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_ShapeDescriptor &shape
    ) {
        tickGhost(entity, revealed, ghost, worldTransform, shape);
    }

    static SystemId create() {
        return registerSystem<
            FOG_REVEAL_EVAL_SHAPE_GHOST,
            IRComponents::C_FogRevealed,
            IRComponents::C_FogGhost,
            IRComponents::C_WorldTransform,
            IRComponents::C_ShapeDescriptor,
            ParallelSafe>("FogRevealEvalShapeGhost");
    }
};

} // namespace IRSystem

namespace IRPrefab::Fog {
inline const auto &heldShapeGhostPoses() {
    using Eval = IRSystem::System<IRSystem::FOG_REVEAL_EVAL_SHAPE_GHOST>;
    static const std::vector<Eval::HeldPose> empty;
    const auto id = IRSystem::findSystem(IRSystem::FOG_REVEAL_EVAL_SHAPE_GHOST);
    return id == IRSystem::kNullSystemId ? empty
                                         : IRSystem::getSystemParams<Eval>(id)->heldGhostPoses_;
}
} // namespace IRPrefab::Fog

#endif /* SYSTEM_FOG_REVEAL_EVAL_SHAPE_H */
