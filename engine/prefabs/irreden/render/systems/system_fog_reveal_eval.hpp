#ifndef SYSTEM_FOG_REVEAL_EVAL_H
#define SYSTEM_FOG_REVEAL_EVAL_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_reveal_settings.hpp>
#include <irreden/render/components/component_fog_ghost.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/fog_settled_gate.hpp>
#include <irreden/common/static_notes.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <irreden/job/worker_block_queue.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace IRSystem {

namespace detail {

inline void applyFogGateToPool(
    IRComponents::C_VoxelSetNew *voxelSet,
    IRComponents::C_VoxelPool *pool,
    bool visible,
    bool ghostHeld
) {
    if (voxelSet == nullptr) {
        return;
    }
    const auto apply = [&](IRComponents::C_VoxelPool &resolvedPool) {
        voxelSet->visible_ = visible;
        voxelSet->ghostHeld_ = ghostHeld;
        // A set its LOD band also hides stays masked off; the LOD gate
        // restores the mask when the band admits it again.
        if (voxelSet->masksActive()) {
            resolvedPool.resyncActiveMaskFromColors(
                voxelSet->voxelStartIdx_,
                static_cast<std::size_t>(voxelSet->allocatedVoxels_)
            );
        } else {
            resolvedPool.clearActiveMaskRange(
                voxelSet->voxelStartIdx_,
                static_cast<std::size_t>(voxelSet->allocatedVoxels_)
            );
        }
    };
    if (pool != nullptr) {
        apply(*pool);
        return;
    }
    IRPrefab::VoxelPool::withPoolByEntity(voxelSet->canvasEntity_, apply);
}

struct FogRevealEvalGhost {
    static constexpr int kGrainSize = kDefaultGrainSize;
    IRPrefab::Fog::SettledGate gate_;

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
        gate_.begin(activeCanvas_, activePool_, fog_, settings_);

        const std::vector<IREntity::ArchetypeNode *> nodes = matchingNodes(gate_.queryExclude());
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

    static std::vector<IREntity::ArchetypeNode *>
    matchingNodes(const IREntity::Archetype &exclude = {}) {
        return IREntity::queryArchetypeNodesSimple(
            IREntity::getArchetype<
                IRComponents::C_FogRevealed,
                IRComponents::C_FogGhost,
                IRComponents::C_WorldTransform,
                IRComponents::C_VoxelSetNew>(),
            exclude
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
        IRComponents::C_FogGhost &ghost
    ) {
        if (!IRPrefab::Fog::isOnFogCanvas(voxelSet, activeCanvas_)) {
            if (!voxelSet.visible_ || voxelSet.ghostHeld_) {
                pending_.push(PendingTransition{&voxelSet, nullptr, true, false});
            }
            return;
        }
        const bool fogGatesAgree =
            voxelSet.visible_ == revealed.shown_ && voxelSet.ghostHeld_ == revealed.ghostHeld_;
        const bool wasShown = revealed.shown_;
        const bool evaluated = revealed.override_ != IRComponents::FogOverride::NONE ||
                               !fogGatesAgree || gate_.nodeSettling_ ||
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

        IRPrefab::Fog::stepGhostLifecycle(
            revealed,
            ghost,
            worldTransform,
            wasShown,
            evaluated,
            settings_.showThreshold_,
            [this](IRMath::vec3 position, std::uint32_t channels) {
                return verdict(position, channels);
            }
        );
        // A set ghost-held at a pose other than its own keeps its ghost
        // positions, so its pool span does not hold the entity's pose yet.
        if (gate_.nodeSettling_ && revealed.ghostHeld_ &&
            !IRPrefab::Fog::samePose(ghost.pose_, worldTransform)) {
            IRPrefab::Static::noteHeld(entity);
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

    void endTick() {
        restampedVoxelsLastFrame_ = 0;
        for (std::uint32_t count : restampedByWorker_) {
            restampedVoxelsLastFrame_ += count;
        }
        pending_.forEach([](const PendingTransition &transition) {
            detail::applyFogGateToPool(
                transition.voxelSet_,
                transition.pool_,
                transition.visible_,
                transition.ghostHeld_
            );
        });
    }

    void tickGhost(
        IREntity::EntityId &entity,
        IRComponents::C_FogRevealed &revealed,
        IRComponents::C_FogGhost &ghost,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_VoxelSetNew &voxelSet
    ) {
        tickImpl(entity, revealed, worldTransform, voxelSet, ghost);
    }
};

} // namespace detail

template <> struct System<FOG_REVEAL_EVAL> {
    static constexpr int kGrainSize = kDefaultGrainSize;
    IRPrefab::Fog::SettledGate gate_;

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
    const IRComponents::C_CanvasFogOfWar *fog_ = nullptr;
    IRComponents::C_VoxelPool *activePool_ = nullptr;
    std::uint64_t frameCounter_ = 0;
    bool fogAttached_ = false;
    IRJob::WorkerBlockQueue<PendingTransition> pending_;
    std::vector<std::uint32_t> restampedByWorker_;
    std::uint32_t restampedVoxelsLastFrame_ = 0;

    static std::vector<IREntity::ArchetypeNode *>
    matchingNodes(IREntity::Archetype exclude = {}) {
        exclude.insert(IREntity::getComponentType<IRComponents::C_FogGhost>());
        return IREntity::queryArchetypeNodesSimple(
            IREntity::getArchetype<
                IRComponents::C_FogRevealed,
                IRComponents::C_WorldTransform,
                IRComponents::C_VoxelSetNew>(),
            exclude
        );
    }

    void beginTick() {
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
        gate_.begin(activeCanvas_, activePool_, fog_, settings_);

        const std::vector<IREntity::ArchetypeNode *> nodes = matchingNodes(gate_.queryExclude());
        std::size_t population = 0;
        for (IREntity::ArchetypeNode *node : nodes) {
            population += static_cast<std::size_t>(node->length_);
        }
        pending_.reset(population);
        if (fog != nullptr) {
            IRPrefab::Fog::touchAnchorRegions(*fog, activeCanvas_, nodes);
        }
        const std::size_t slots = static_cast<std::size_t>(IRJob::workerCount()) + 1u;
        restampedByWorker_.assign(slots, 0u);
    }

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

    void tick(
        IREntity::EntityId &entity,
        IRComponents::C_FogRevealed &revealed,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_VoxelSetNew &voxelSet
    ) {
        if (!IRPrefab::Fog::isOnFogCanvas(voxelSet, activeCanvas_)) {
            if (!voxelSet.visible_ || voxelSet.ghostHeld_) {
                pending_.push(PendingTransition{&voxelSet, nullptr, true, false});
            }
            return;
        }
        const bool fogGatesAgree = voxelSet.visible_ == revealed.shown_ && !voxelSet.ghostHeld_;
        if (revealed.override_ == IRComponents::FogOverride::NONE && fogGatesAgree &&
            !gate_.nodeSettling_ && (entity + frameCounter_) % settings_.staggerPeriod_ != 0u) {
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
        if (shown == revealed.shown_ && fogGatesAgree) {
            return;
        }
        // Commit the verdict only when endTick can apply the matching mask and
        // visibility transition; otherwise the next frame must retry it.
        if (activePool_ == nullptr) {
            return;
        }
        revealed.shown_ = shown;
        pending_.push(PendingTransition{&voxelSet, activePool_, shown, false});
    }

    void endTick() {
        restampedVoxelsLastFrame_ = 0;
        for (std::uint32_t count : restampedByWorker_) {
            restampedVoxelsLastFrame_ += count;
        }
        pending_.forEach([](const PendingTransition &transition) {
            detail::applyFogGateToPool(
                transition.voxelSet_,
                transition.pool_,
                transition.visible_,
                transition.ghostHeld_
            );
        });
    }

    void tick(
        const IREntity::Archetype &type,
        std::vector<IREntity::EntityId> &entities,
        std::vector<IRComponents::C_FogRevealed> &revealed,
        std::vector<IRComponents::C_WorldTransform> &worldTransforms,
        std::vector<IRComponents::C_VoxelSetNew> &voxelSets
    ) {
        gate_.walkNode(*this, kGrainSize, type, entities, revealed, worldTransforms, voxelSets);
    }

    static SystemId create() {
        return registerSystem<
            FOG_REVEAL_EVAL,
            IRComponents::C_FogRevealed,
            const IRComponents::C_WorldTransform,
            IRComponents::C_VoxelSetNew,
            Exclude<IRComponents::C_FogGhost>,
            GatesSettled,
            MainThread>("FogRevealEval");
    }
};

template <> struct System<FOG_REVEAL_EVAL_GHOST> : detail::FogRevealEvalGhost {
    void tick(
        IREntity::EntityId &entity,
        IRComponents::C_FogRevealed &revealed,
        IRComponents::C_FogGhost &ghost,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_VoxelSetNew &voxelSet
    ) {
        tickGhost(entity, revealed, ghost, worldTransform, voxelSet);
    }

    void tick(
        const IREntity::Archetype &type,
        std::vector<IREntity::EntityId> &entities,
        std::vector<IRComponents::C_FogRevealed> &revealed,
        std::vector<IRComponents::C_FogGhost> &ghosts,
        std::vector<IRComponents::C_WorldTransform> &worldTransforms,
        std::vector<IRComponents::C_VoxelSetNew> &voxelSets
    ) {
        gate_.walkNode(
            *this,
            kGrainSize,
            type,
            entities,
            revealed,
            ghosts,
            worldTransforms,
            voxelSets
        );
    }

    static SystemId create() {
        return registerSystem<
            FOG_REVEAL_EVAL_GHOST,
            IRComponents::C_FogRevealed,
            IRComponents::C_FogGhost,
            const IRComponents::C_WorldTransform,
            IRComponents::C_VoxelSetNew,
            GatesSettled,
            MainThread>("FogRevealEvalGhost");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FOG_REVEAL_EVAL_H */
