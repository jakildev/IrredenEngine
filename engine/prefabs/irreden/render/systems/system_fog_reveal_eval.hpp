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
    IRComponents::C_VoxelPool *activePool_ = nullptr;
    std::uint64_t frameCounter_ = 0;
    bool fogAttached_ = false;
    std::vector<std::vector<PendingTransition>> pendingByWorker_;

    void beginTick() {
        activeCanvas_ = IRRender::getActiveCanvasEntityOrNull();
        activePool_ = nullptr;
        fogAttached_ = false;
        observers_ = {};

        if (activeCanvas_ != IREntity::kNullEntity) {
            if (auto pool =
                    IREntity::getComponentOptional<IRComponents::C_VoxelPool>(activeCanvas_)) {
                activePool_ = *pool;
            }
            if (auto fog =
                    IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(activeCanvas_)) {
                observers_ = (*fog)->observers_;
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
        const std::size_t transitionsPerWorker =
            (static_cast<std::size_t>(IREntity::getLiveEntityCount()) + slots - 1u) / slots;
        for (std::vector<PendingTransition> &worker : pendingByWorker_) {
            worker.clear();
            worker.reserve(transitionsPerWorker);
        }
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

        revealed.revealFactor_ =
            fogAttached_ ? IRPrefab::Fog::evalVisionReveal(observers_, worldTransform.translation_)
                         : 1.0f;
        bool shown = revealed.shown_;
        if (!shown && revealed.revealFactor_ >= settings_.showThreshold_) {
            shown = true;
        } else if (shown && revealed.revealFactor_ <= settings_.hideThreshold_) {
            shown = false;
        }
        if (shown == revealed.shown_) {
            return;
        }
        revealed.shown_ = shown;
        pendingByWorker_[static_cast<std::size_t>(IRJob::workerId())].push_back(
            PendingTransition{&voxelSet, activePool_, shown}
        );
    }

    void endTick() {
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
