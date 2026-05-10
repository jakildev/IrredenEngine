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
/// kernels iterate. The CPU side keeps a parallel staging vector so spawn
/// requests batch into one upload per frame; dead slots (`lifetime_ <= 0`)
/// are reclaimed by `reserveSlot` via a linear scan and become the spawn
/// target for the next request.
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
        , dirty_{true}
        , nextSearchIndex_{0u} {}

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
                dirty_ = true;
                return idx;
            }
        }
        return kInvalidSlot;
    }

    /// Direct write into a reserved slot. Caller must hold a slot returned
    /// by `reserveSlot`. The CPU buffer is the source of truth; the next
    /// `UPDATE_GPU_PARTICLES` tick uploads the dirty range.
    void writeSlot(std::uint32_t index, const IRRender::GpuParticle &particle) {
        particles_[index] = particle;
        dirty_ = true;
    }

    /// Mark every slot as dead. Convenience for tests and demo resets.
    void clear() {
        for (auto &p : particles_) {
            p.lifetime_ = 0.0f;
        }
        nextSearchIndex_ = 0u;
        dirty_ = true;
    }

    static constexpr std::uint32_t kInvalidSlot = static_cast<std::uint32_t>(-1);

    std::uint32_t capacity_ = 0u;
    std::vector<IRRender::GpuParticle> particles_;
    std::pair<IRRender::ResourceId, IRRender::Buffer *> buffer_{0u, nullptr};
    bool dirty_ = false;
    std::uint32_t nextSearchIndex_ = 0u;
};

} // namespace IRComponents

#endif /* COMPONENT_GPU_PARTICLE_POOL_H */
