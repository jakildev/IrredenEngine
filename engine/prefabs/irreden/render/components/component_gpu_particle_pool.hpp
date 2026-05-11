#ifndef COMPONENT_GPU_PARTICLE_POOL_H
#define COMPONENT_GPU_PARTICLE_POOL_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/ir_render_types.hpp>

#include <cstdint>
#include <utility>
#include <vector>

using namespace IRMath;

namespace IRComponents {

/// GPU-resident particle pool (T-139 Phase 1). Stores up to
/// `kGpuParticlePoolCapacity` particles in a single SSBO that the
/// `UPDATE_GPU_PARTICLES` and `RENDER_GPU_PARTICLES_TO_TRIXEL` compute
/// kernels iterate. After construction the SSBO is the source of truth —
/// the GPU update kernel mutates position and lifetime every frame, so
/// the CPU never reads back. Spawn requests (`writeSlot`) push a single
/// particle to the SSBO via `subData` immediately; there is no per-frame
/// CPU→GPU mirror upload, and consequently no dirty flag (per project
/// rule `cpp-ecs.md` "No dirty flags on components").
///
/// The CPU-side `particles_` vector exists only to drive `reserveSlot`'s
/// dead-slot search — its `lifetime_` field does not track the GPU's
/// per-frame decay, so a slot stays "taken" from the CPU's view until
/// `clear()` is called.
///
/// One pool entity per canvas — attach this component to the canvas you want
/// the particles rendered into. The render system reads the canvas's own
/// `C_TriangleCanvasTextures` because both components live on the same
/// archetype.
struct C_GPUParticlePool {
    C_GPUParticlePool(std::uint32_t capacity)
        : capacity_{capacity}
        , particles_(capacity)
        , buffer_{IRRender::createResource<IRRender::Buffer>(
              nullptr,
              sizeof(IRRender::GpuParticle) * capacity,
              IRRender::BUFFER_STORAGE_DYNAMIC,
              IRRender::BufferTarget::SHADER_STORAGE,
              IRRender::kBufferIndex_GpuParticleData
          )}
        , nextSearchIndex_{0u} {
        // Seed the SSBO with the all-zero CPU state so the update kernel's
        // `lifetime <= 0` early-out is well-defined on frame 0 — the
        // `createResource(nullptr, ...)` call above only allocates storage.
        if (buffer_.second != nullptr && capacity_ > 0u) {
            buffer_.second
                ->subData(0, sizeof(IRRender::GpuParticle) * capacity_, particles_.data());
        }
    }

    C_GPUParticlePool()
        : C_GPUParticlePool(IRRender::kGpuParticlePoolCapacity) {}

    void onDestroy() {
        IRRender::destroyResource<IRRender::Buffer>(buffer_.first);
    }

    /// Reserve the next dead slot and return its index. If all slots are
    /// alive the request is silently dropped (returns `kInvalidSlot`).
    /// Linear-scan reclamation: the search wraps around from
    /// `nextSearchIndex_` so consecutive spawns don't all O(N²) re-scan
    /// from zero.
    std::uint32_t reserveSlot() {
        for (std::uint32_t step = 0; step < capacity_; ++step) {
            const std::uint32_t idx = (nextSearchIndex_ + step) % capacity_;
            if (particles_[idx].lifetime_ <= 0.0f) {
                nextSearchIndex_ = (idx + 1) % capacity_;
                return idx;
            }
        }
        return kInvalidSlot;
    }

    /// Direct write into a reserved slot. Mirrors into the CPU staging
    /// vector (so subsequent `reserveSlot` calls skip the slot) and
    /// uploads the single slot to the GPU SSBO in the same call — no
    /// per-frame sync step is needed.
    void writeSlot(std::uint32_t index, const IRRender::GpuParticle &particle) {
        particles_[index] = particle;
        if (buffer_.second != nullptr) {
            buffer_.second->subData(
                sizeof(IRRender::GpuParticle) * index,
                sizeof(IRRender::GpuParticle),
                &particle
            );
        }
    }

    /// Mark every slot as dead on both CPU and GPU. Uploads the cleared
    /// CPU buffer to the SSBO in one `subData` call.
    void clear() {
        for (auto &p : particles_) {
            p.lifetime_ = 0.0f;
        }
        nextSearchIndex_ = 0u;
        if (buffer_.second != nullptr && capacity_ > 0u) {
            buffer_.second
                ->subData(0, sizeof(IRRender::GpuParticle) * capacity_, particles_.data());
        }
    }

    static constexpr std::uint32_t kInvalidSlot = static_cast<std::uint32_t>(-1);

    std::uint32_t capacity_ = 0u;
    std::vector<IRRender::GpuParticle> particles_;
    std::pair<IRRender::ResourceId, IRRender::Buffer *> buffer_{0u, nullptr};
    std::uint32_t nextSearchIndex_ = 0u;
};

} // namespace IRComponents

#endif /* COMPONENT_GPU_PARTICLE_POOL_H */
