#ifndef SYSTEM_FOG_REVEAL_EVAL_H
#define SYSTEM_FOG_REVEAL_EVAL_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_reveal_settings.hpp>
#include <irreden/render/components/component_fog_ghost.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <irreden/job/worker_block_queue.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace IRSystem {

namespace detail {

template <bool kGhost> struct FogRevealEval {
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    struct PendingTransition {
        IRComponents::C_VoxelSetNew *voxelSet_ = nullptr;
        IRComponents::C_VoxelPool *pool_ = nullptr;
        bool visible_ = false;
        bool ghostHeld_ = false;
    };

    IRComponents::FrameDataFogObservers observers_{};
    IRComponents::FogLosColumnField los_{};
    IRPrefab::Fog::LosHardRouteCache losRoutes_;
    IRComponents::C_FogRevealSettings settings_{};
    IREntity::EntityId activeCanvas_ = IREntity::kNullEntity;
    // Grid taps for the verdict; null when no fog is attached, in which case
    // `observers_` alone (empty) drives an unrestricted verdict.
    const IRComponents::C_CanvasFogOfWar *fog_ = nullptr;
    IRComponents::C_VoxelPool *activePool_ = nullptr;
    std::uint64_t frameCounter_ = 0;
    bool fogAttached_ = false;
    IRJob::WorkerBlockQueue<PendingTransition> pending_;
    // Voxels whose carrier factor was rewritten this frame, per worker, summed
    // into `restampedVoxelsLastFrame_` in endTick for perf probes.
    std::vector<std::uint32_t> restampedByWorker_;
    std::uint32_t restampedVoxelsLastFrame_ = 0;

    void beginTick() {
        IR_PROFILE_SCOPE(kGhost ? "FogRevealEvalGhost.begin" : "FogRevealEval.begin");
        activeCanvas_ = IRRender::getActiveCanvasEntityOrNull();
        activePool_ = nullptr;
        fogAttached_ = false;
        fog_ = nullptr;
        observers_ = {};
        los_ = {};
        IRComponents::C_CanvasFogOfWar *fog = nullptr;

        if (activeCanvas_ != IREntity::kNullEntity) {
            if (auto pool =
                    IREntity::getComponentOptional<IRComponents::C_VoxelPool>(activeCanvas_)) {
                activePool_ = *pool;
            }
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
                fogAttached_ = true;
            }
        }
        losRoutes_.begin(observers_, los_);
        settings_ = IREntity::singleton<IRComponents::C_FogRevealSettings>();
        settings_.staggerPeriod_ = IRMath::max(settings_.staggerPeriod_, std::uint32_t{1});
        ++frameCounter_;

        const std::vector<IREntity::ArchetypeNode *> nodes = matchingNodes();
        std::size_t population = 0;
        for (IREntity::ArchetypeNode *node : nodes) {
            population += static_cast<std::size_t>(node->length_);
        }
        pending_.reset(population);
        // No stagger filter: every anchor's region keeps its access bit each
        // frame, so the gather's eviction keeps a far region a body reads.
        if (fog != nullptr) {
            IRPrefab::Fog::touchAnchorRegions(*fog, activeCanvas_, nodes);
        }
        const std::size_t slots = static_cast<std::size_t>(IRJob::workerCount()) + 1u;
        restampedByWorker_.assign(slots, 0u);
    }

    static std::vector<IREntity::ArchetypeNode *> matchingNodes() {
        if constexpr (kGhost) {
            return IREntity::queryArchetypeNodesSimple(
                IREntity::getArchetype<
                    IRComponents::C_FogRevealed,
                    IRComponents::C_FogGhost,
                    IRComponents::C_WorldTransform,
                    IRComponents::C_VoxelSetNew>()
            );
        }
        return IREntity::queryArchetypeNodesSimple(
            IREntity::getArchetype<
                IRComponents::C_FogRevealed,
                IRComponents::C_WorldTransform,
                IRComponents::C_VoxelSetNew>(),
            IREntity::getArchetype<IRComponents::C_FogGhost>()
        );
    }

    // The ground-anchor verdict on this frame's snapshot: the grid term when a
    // fog component is attached, else the circle term over the observers a
    // test seeded.
    float verdict(IRMath::vec3 worldPosition, std::uint32_t channels) {
        if (!fogAttached_) {
            return 1.0f;
        }
        if (fog_ != nullptr) {
            return IRPrefab::Fog::evalReveal(
                *fog_,
                observers_,
                los_,
                losRoutes_,
                worldPosition,
                channels
            );
        }
        return IRPrefab::Fog::evalVisionReveal(
            observers_,
            los_,
            losRoutes_,
            worldPosition,
            channels
        );
    }

    void tickImpl(
        IREntity::EntityId &entity,
        IRComponents::C_FogRevealed &revealed,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_VoxelSetNew &voxelSet,
        IRComponents::C_FogGhost *ghost
    ) {
        if (!IRPrefab::Fog::isOnFogCanvas(voxelSet, activeCanvas_)) {
            return;
        }
        const bool wasShown = revealed.shown_;
        const bool evaluated = revealed.override_ != IRComponents::FogOverride::NONE ||
                               (entity + frameCounter_) % settings_.staggerPeriod_ == 0u;
        if (evaluated) {
            bool shown = revealed.shown_;
            if (revealed.override_ == IRComponents::FogOverride::FORCE_REVEALED) {
                revealed.revealFactor_ = 1.0f;
                shown = true;
            } else if (revealed.override_ == IRComponents::FogOverride::FORCE_HIDDEN) {
                revealed.revealFactor_ = 0.0f;
                shown = false;
            } else {
                revealed.revealFactor_ = verdict(worldTransform.translation_, revealed.channels_);
                if (!shown && revealed.revealFactor_ >= settings_.showThreshold_) {
                    shown = true;
                } else if (shown && revealed.revealFactor_ <= settings_.hideThreshold_) {
                    shown = false;
                }
            }
            if (shown != revealed.shown_ && activePool_ != nullptr) {
                revealed.shown_ = shown;
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
                    return verdict(position, channels);
                }
            );
        }

        if ((revealed.shown_ || revealed.ghostHeld_) && activePool_ != nullptr) {
            const std::uint8_t factor =
                revealed.ghostHeld_ ? IRComponents::kFogStateExplored
                                    : IRPrefab::Fog::quantizeRevealFactor(revealed.revealFactor_);
            const std::uint32_t stamped = IRPrefab::Fog::bodyCarrierBits(*activePool_, voxelSet) >>
                                          IRComponents::VoxelReserved::kFogBodyFactorShift;
            if (stamped != factor) {
                IRPrefab::Fog::stampBodyCarrier(*activePool_, voxelSet, true, factor);
                const auto slot = static_cast<std::size_t>(IRJob::workerId());
                if (slot < restampedByWorker_.size()) {
                    restampedByWorker_[slot] += static_cast<std::uint32_t>(voxelSet.numVoxels_);
                }
            }
        }
        if (activePool_ != nullptr &&
            (voxelSet.visible_ != revealed.shown_ || voxelSet.ghostHeld_ != revealed.ghostHeld_)) {
            pending_.push(
                PendingTransition{&voxelSet, activePool_, revealed.shown_, revealed.ghostHeld_}
            );
        }
    }

    void tick(
        IREntity::EntityId &entity,
        IRComponents::C_FogRevealed &revealed,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_VoxelSetNew &voxelSet
    ) {
        if (!IRPrefab::Fog::isOnFogCanvas(voxelSet, activeCanvas_)) {
            return;
        }
        if (revealed.override_ == IRComponents::FogOverride::NONE &&
            (entity + frameCounter_) % settings_.staggerPeriod_ != 0u) {
            return;
        }

        bool shown = revealed.shown_;
        if (revealed.override_ == IRComponents::FogOverride::FORCE_REVEALED) {
            revealed.revealFactor_ = 1.0f;
            shown = true;
        } else if (revealed.override_ == IRComponents::FogOverride::FORCE_HIDDEN) {
            revealed.revealFactor_ = 0.0f;
            shown = false;
        } else {
            revealed.revealFactor_ = verdict(worldTransform.translation_, revealed.channels_);
            if (!shown && revealed.revealFactor_ >= settings_.showThreshold_) {
                shown = true;
            } else if (shown && revealed.revealFactor_ <= settings_.hideThreshold_) {
                shown = false;
            }
        }
        if (shown && activePool_ != nullptr) {
            const std::uint8_t factor = IRPrefab::Fog::quantizeRevealFactor(revealed.revealFactor_);
            const std::uint32_t stamped = IRPrefab::Fog::bodyCarrierBits(*activePool_, voxelSet) >>
                                          IRComponents::VoxelReserved::kFogBodyFactorShift;
            if (stamped != factor) {
                IRPrefab::Fog::stampBodyCarrier(*activePool_, voxelSet, true, factor);
                const auto slot = static_cast<std::size_t>(IRJob::workerId());
                if (slot < restampedByWorker_.size()) {
                    restampedByWorker_[slot] += static_cast<std::uint32_t>(voxelSet.numVoxels_);
                }
            }
        }
        if (shown == revealed.shown_ || activePool_ == nullptr) {
            return;
        }
        revealed.shown_ = shown;
        pending_.push(PendingTransition{&voxelSet, activePool_, shown, false});
    }

    void endTick() {
        IR_PROFILE_SCOPE(kGhost ? "FogRevealEvalGhost.end" : "FogRevealEval.end");
        restampedVoxelsLastFrame_ = 0;
        for (std::uint32_t count : restampedByWorker_) {
            restampedVoxelsLastFrame_ += count;
        }
        pending_.forEach([](const PendingTransition &transition) {
            if (transition.voxelSet_ == nullptr || transition.pool_ == nullptr) {
                return;
            }
            IRComponents::C_VoxelSetNew &voxelSet = *transition.voxelSet_;
            voxelSet.visible_ = transition.visible_;
            voxelSet.ghostHeld_ = transition.ghostHeld_;
            // A set its LOD band also hides stays masked off; the LOD gate
            // restores the mask when the band admits it again.
            if (voxelSet.masksActive()) {
                transition.pool_->resyncActiveMaskFromColors(
                    voxelSet.voxelStartIdx_,
                    static_cast<std::size_t>(voxelSet.numVoxels_)
                );
            } else {
                transition.pool_->clearActiveMaskRange(
                    voxelSet.voxelStartIdx_,
                    static_cast<std::size_t>(voxelSet.numVoxels_)
                );
            }
        });
    }

    void tickGhost(
        IREntity::EntityId &entity,
        IRComponents::C_FogRevealed &revealed,
        IRComponents::C_FogGhost &ghost,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_VoxelSetNew &voxelSet
    ) {
        tickImpl(entity, revealed, worldTransform, voxelSet, &ghost);
    }
};

} // namespace detail

template <> struct System<FOG_REVEAL_EVAL> : detail::FogRevealEval<false> {
    static SystemId create() {
        return registerSystem<
            FOG_REVEAL_EVAL,
            IRComponents::C_FogRevealed,
            IRComponents::C_WorldTransform,
            IRComponents::C_VoxelSetNew,
            Exclude<IRComponents::C_FogGhost>,
            ParallelSafe>("FogRevealEval");
    }
};

template <> struct System<FOG_REVEAL_EVAL_GHOST> : detail::FogRevealEval<true> {
    void tick(
        IREntity::EntityId &entity,
        IRComponents::C_FogRevealed &revealed,
        IRComponents::C_FogGhost &ghost,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_VoxelSetNew &voxelSet
    ) {
        tickGhost(entity, revealed, ghost, worldTransform, voxelSet);
    }

    static SystemId create() {
        return registerSystem<
            FOG_REVEAL_EVAL_GHOST,
            IRComponents::C_FogRevealed,
            IRComponents::C_FogGhost,
            IRComponents::C_WorldTransform,
            IRComponents::C_VoxelSetNew,
            ParallelSafe>("FogRevealEvalGhost");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FOG_REVEAL_EVAL_H */
