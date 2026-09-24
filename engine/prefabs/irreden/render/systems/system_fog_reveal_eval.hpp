#ifndef SYSTEM_FOG_REVEAL_EVAL_H
#define SYSTEM_FOG_REVEAL_EVAL_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_reveal_settings.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace IRSystem {

template <> struct System<FOG_REVEAL_EVAL> {
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    struct PendingTransition {
        IRComponents::C_VoxelSetNew *voxelSet_ = nullptr;
        IRComponents::C_VoxelPool *pool_ = nullptr;
        bool visible_ = false;
    };

    IRComponents::FrameDataFogObservers observers_{};
    IRComponents::C_FogRevealSettings settings_{};
    IREntity::EntityId activeCanvas_ = IREntity::kNullEntity;
    // Grid taps for the verdict; null when no fog is attached, in which case
    // `observers_` alone (empty) drives an unrestricted verdict.
    const IRComponents::C_CanvasFogOfWar *fog_ = nullptr;
    IRComponents::C_VoxelPool *activePool_ = nullptr;
    std::uint64_t frameCounter_ = 0;
    bool fogAttached_ = false;
    std::vector<std::vector<PendingTransition>> pendingByWorker_;
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

        if (activeCanvas_ != IREntity::kNullEntity) {
            if (auto pool =
                    IREntity::getComponentOptional<IRComponents::C_VoxelPool>(activeCanvas_)) {
                activePool_ = *pool;
            }
            if (auto fog =
                    IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(activeCanvas_)) {
                fog_ = *fog;
                observers_ = fog_->observers_;
                fogAttached_ = true;
            }
        }
        settings_ = IREntity::singleton<IRComponents::C_FogRevealSettings>();
        settings_.staggerPeriod_ = IRMath::max(settings_.staggerPeriod_, std::uint32_t{1});
        ++frameCounter_;

        const std::size_t slots = static_cast<std::size_t>(IRJob::workerCount()) + 1u;
        if (pendingByWorker_.size() < slots) {
            pendingByWorker_.resize(slots);
        }
        for (std::vector<PendingTransition> &worker : pendingByWorker_) {
            worker.clear();
        }
        restampedByWorker_.assign(slots, 0u);
    }

    // The ground-anchor verdict: the grid term when a fog component is
    // attached, else the circle term over the observers a test seeded.
    float verdict(IRMath::vec3 worldPosition) const {
        if (!fogAttached_) {
            return 1.0f;
        }
        if (fog_ != nullptr) {
            return IRPrefab::Fog::evalReveal(*fog_, worldPosition);
        }
        return IRPrefab::Fog::evalVisionReveal(observers_, worldPosition);
    }

    void tick(
        IREntity::EntityId &entity,
        IRComponents::C_FogRevealed &revealed,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_VoxelSetNew &voxelSet
    ) {
        if ((entity + frameCounter_) % settings_.staggerPeriod_ != 0u) {
            return;
        }
        if (voxelSet.canvasEntity_ != IREntity::kNullEntity &&
            voxelSet.canvasEntity_ != activeCanvas_) {
            return;
        }

        revealed.revealFactor_ = verdict(worldTransform.translation_);
        bool shown = revealed.shown_;
        if (!shown && revealed.revealFactor_ >= settings_.showThreshold_) {
            shown = true;
        } else if (shown && revealed.revealFactor_ <= settings_.hideThreshold_) {
            shown = false;
        }
        // A shown body renders at its carrier factor, so the carrier follows
        // the verdict whenever its 8-bit form moves; a hidden body's carrier
        // is unobservable and left alone.
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
        if (shown == revealed.shown_) {
            return;
        }
        // Commit the verdict only when endTick can apply the matching mask and
        // visibility transition; otherwise the next frame must retry it.
        if (activePool_ == nullptr) {
            return;
        }
        revealed.shown_ = shown;
        pendingByWorker_[static_cast<std::size_t>(IRJob::workerId())].push_back(
            PendingTransition{&voxelSet, activePool_, shown}
        );
    }

    void endTick() {
        restampedVoxelsLastFrame_ = 0;
        for (std::uint32_t count : restampedByWorker_) {
            restampedVoxelsLastFrame_ += count;
        }
        for (std::vector<PendingTransition> &worker : pendingByWorker_) {
            for (const PendingTransition &transition : worker) {
                if (transition.voxelSet_ == nullptr || transition.pool_ == nullptr) {
                    continue;
                }
                IRComponents::C_VoxelSetNew &voxelSet = *transition.voxelSet_;
                voxelSet.visible_ = transition.visible_;
                if (transition.visible_) {
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
            }
        }
    }

    static SystemId create() {
        return registerSystem<
            FOG_REVEAL_EVAL,
            IRComponents::C_FogRevealed,
            IRComponents::C_WorldTransform,
            IRComponents::C_VoxelSetNew,
            ParallelSafe>("FogRevealEval");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FOG_REVEAL_EVAL_H */
