#ifndef SYSTEM_FOG_SUBJECT_ADOPT_H
#define SYSTEM_FOG_SUBJECT_ADOPT_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_exempt.hpp>
#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/render/components/component_fog_reveal_settings.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <cstddef>
#include <vector>

namespace IRSystem {

// BODY is the default fog subject class: every voxel set on the fogged active
// canvas that carries no class marker and no BODY state is adopted here within
// one frame. Adoption takes the ground-anchor verdict through the shared
// oracle, stamps the class bit and the quantized factor on the set's voxels,
// clears the pool mask for a hidden verdict in endTick, and defers
// `C_FogRevealed`, so the entity migrates archetype once at the structural
// flush and FOG_REVEAL_EVAL owns it from the next frame. Sits immediately
// before FOG_REVEAL_EVAL, after PROPAGATE_TRANSFORM. Inert while the active
// canvas carries no fog component or no pool.
template <> struct System<FOG_SUBJECT_ADOPT> {
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    struct PendingHide {
        IRComponents::C_VoxelSetNew *voxelSet_ = nullptr;
        IRComponents::C_VoxelPool *pool_ = nullptr;
    };

    IREntity::EntityId activeCanvas_ = IREntity::kNullEntity;
    const IRComponents::C_CanvasFogOfWar *fog_ = nullptr;
    IRComponents::C_VoxelPool *activePool_ = nullptr;
    IRComponents::C_FogRevealSettings settings_{};
    std::vector<std::vector<PendingHide>> pendingByWorker_;

    void beginTick() {
        activeCanvas_ = IRRender::getActiveCanvasEntityOrNull();
        fog_ = nullptr;
        activePool_ = nullptr;
        if (activeCanvas_ != IREntity::kNullEntity) {
            if (auto fog =
                    IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(activeCanvas_)) {
                fog_ = *fog;
            }
            if (auto pool =
                    IREntity::getComponentOptional<IRComponents::C_VoxelPool>(activeCanvas_)) {
                activePool_ = *pool;
            }
        }
        settings_ = IREntity::singleton<IRComponents::C_FogRevealSettings>();

        const std::size_t slots = static_cast<std::size_t>(IRJob::workerCount()) + 1u;
        if (pendingByWorker_.size() < slots) {
            pendingByWorker_.resize(slots);
        }
        for (std::vector<PendingHide> &worker : pendingByWorker_) {
            worker.clear();
        }
    }

    void tick(
        IREntity::EntityId &entity,
        const IRComponents::C_WorldTransform &worldTransform,
        IRComponents::C_VoxelSetNew &voxelSet
    ) {
        if (fog_ == nullptr || activePool_ == nullptr) {
            return;
        }
        if (voxelSet.canvasEntity_ != IREntity::kNullEntity &&
            voxelSet.canvasEntity_ != activeCanvas_) {
            return;
        }

        IRComponents::C_FogRevealed revealed{};
        revealed.revealFactor_ = IRPrefab::Fog::evalReveal(*fog_, worldTransform.translation_);
        revealed.shown_ = revealed.revealFactor_ >= settings_.showThreshold_;
        IRPrefab::Fog::stampBodyCarrier(
            *activePool_,
            voxelSet,
            true,
            IRPrefab::Fog::quantizeRevealFactor(revealed.revealFactor_)
        );
        if (!revealed.shown_) {
            pendingByWorker_[static_cast<std::size_t>(IRJob::workerId())].push_back(
                PendingHide{&voxelSet, activePool_}
            );
        }
        IREntity::setComponentDeferred(entity, revealed);
    }

    void endTick() {
        for (std::vector<PendingHide> &worker : pendingByWorker_) {
            for (const PendingHide &hide : worker) {
                IRComponents::C_VoxelSetNew &voxelSet = *hide.voxelSet_;
                voxelSet.visible_ = false;
                hide.pool_->clearActiveMaskRange(
                    voxelSet.voxelStartIdx_,
                    static_cast<std::size_t>(voxelSet.numVoxels_)
                );
            }
        }
    }

    static SystemId create() {
        return registerSystem<
            FOG_SUBJECT_ADOPT,
            IRComponents::C_WorldTransform,
            IRComponents::C_VoxelSetNew,
            Exclude<IRComponents::C_FogRevealed>,
            Exclude<IRComponents::C_FogField>,
            Exclude<IRComponents::C_FogExempt>,
            ParallelSafe>("FogSubjectAdopt");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FOG_SUBJECT_ADOPT_H */
