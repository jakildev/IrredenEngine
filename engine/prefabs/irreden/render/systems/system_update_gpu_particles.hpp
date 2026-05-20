#ifndef SYSTEM_UPDATE_GPU_PARTICLES_H
#define SYSTEM_UPDATE_GPU_PARTICLES_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/components/component_gpu_particle_pool.hpp>

#include <memory>
#include <vector>

using namespace IRComponents;
using namespace IRRender;

namespace IRSystem {

/// T-139 Phase 1 — GPU particle update pass; T-159 Phase 2 — folds in
/// the per-frame spawn flush so per-spawn Metal `subData` orphan churn
/// is replaced by one coalesced upload per frame.
///
/// Per matched canvas (one entity owning a `C_GPUParticlePool`):
///   1. Flush any spawn writes queued this frame via
///      `C_GPUParticlePool::writeSlot` — one `subData` per contiguous
///      run of touched slot indices; typical case is one call total.
///   2. Upload per-frame data (deltaTime, particleCount).
///   3. Bind particle SSBO + UBO; dispatch the update kernel with one thread
///      per particle slot.
///
/// `writeSlot` no longer touches the SSBO directly — the flush at step
/// 1 is the only CPU→GPU sync, and it only uploads slots that were
/// just written this frame, never untouched ranges (the CPU mirror's
/// position/lifetime is stale relative to the GPU update kernel).
///
/// Pipeline placement: belongs in the RENDER pipeline ahead of the particle
/// render pass (so positions and lifetimes advance before the rasterization
/// reads them). Any system that calls `IRPrefab::GpuParticles::spawn`
/// must run before this one so its writes land in the same-frame flush.
/// Recommended slot: between `SHAPES_TO_TRIXEL` and
/// `RENDER_GPU_PARTICLES_TO_TRIXEL`.
template <> struct System<UPDATE_GPU_PARTICLES> {
    struct Params {
        ShaderProgram *updateProgram_ = nullptr;
        Buffer *frameDataBuf_ = nullptr;
        FrameDataGpuParticles frameData_{};
    };

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "GpuParticleUpdateProgram",
            std::vector{ShaderStage{IRRender::kFileCompUpdateGpuParticles, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<Buffer>(
            "GpuParticleFrameData",
            nullptr,
            sizeof(FrameDataGpuParticles),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FrameDataGpuParticles
        );

        auto paramsOwner = std::make_unique<Params>();
        Params *p = paramsOwner.get();
        p->updateProgram_ = IRRender::getNamedResource<ShaderProgram>("GpuParticleUpdateProgram");
        p->frameDataBuf_ = IRRender::getNamedResource<Buffer>("GpuParticleFrameData");

        SystemId systemId =
            createSystem<C_GPUParticlePool>("UpdateGpuParticles", [p](C_GPUParticlePool &pool) {
                if (pool.capacity_ == 0u || pool.buffer_.second == nullptr)
                    return;

                // Phase 2 (T-159): flush spawns queued this frame in one
                // coalesced upload before the update kernel reads the SSBO.
                pool.flushPendingSpawns();

                p->frameData_.deltaTime_ =
                    static_cast<float>(IRTime::deltaTime(IRTime::Events::RENDER));
                p->frameData_.particleCount_ = pool.capacity_;
                p->frameDataBuf_->subData(0, sizeof(FrameDataGpuParticles), &p->frameData_);

                p->updateProgram_->use();
                pool.buffer_.second->bindBase(
                    BufferTarget::SHADER_STORAGE,
                    kBufferIndex_GpuParticleData
                );
                p->frameDataBuf_->bindBase(
                    BufferTarget::UNIFORM,
                    kBufferIndex_FrameDataGpuParticles
                );

                constexpr std::uint32_t kLocalSize = 64u;
                const std::uint32_t groups = (pool.capacity_ + kLocalSize - 1u) / kLocalSize;
                IRRender::device()->dispatchCompute(groups, 1u, 1u);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
            });

        setSystemParams(systemId, std::move(paramsOwner));
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_UPDATE_GPU_PARTICLES_H */
