#ifndef COMPONENT_GPU_PARTICLE_POOL_H
#define COMPONENT_GPU_PARTICLE_POOL_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/ir_render_types.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

using namespace IRMath;

namespace IRComponents {

/// GPU-resident particle pool (T-139 Phase 1, T-159 Phase 2 batching).
/// Stores up to `kGpuParticlePoolCapacity` particles in a single SSBO
/// that the `UPDATE_GPU_PARTICLES` and `RENDER_GPU_PARTICLES_TO_TRIXEL`
/// compute kernels iterate. After construction the SSBO is the source
/// of truth — the GPU update kernel mutates position and lifetime every
/// frame, so the CPU never reads back.
///
/// Spawn requests (`writeSlot`) append the slot index to a per-frame
/// pending list and mirror the particle into the CPU `particles_`
/// vector. The compaction `flushPendingSpawns()` is called once per
/// frame from `UPDATE_GPU_PARTICLES` and issues one `Buffer::subData`
/// per contiguous run of pending indices. The common case —
/// `reserveSlot()`'s linear-scan reclamation hands out sequential
/// indices — collapses to exactly one `subData/frame` regardless of
/// spawn count, eliminating the per-spawn Metal orphan churn that
/// would otherwise dominate continuous-emitter workloads.
///
/// We never re-upload "untouched" contiguous ranges because the CPU
/// mirror's position/lifetime is stale between mutations — only the
/// freshly-written slots are uploaded, so GPU-authored state is
/// preserved (per project rule `cpp-ecs.md` "No dirty flags on
/// components"; the pending list is a sparse upload manifest, not a
/// dirty flag).
///
/// The CPU-side `particles_` vector exists for two reasons: it drives
/// `reserveSlot`'s dead-slot search (its `lifetime_` does not track
/// the GPU's per-frame decay, so a slot stays "taken" from the CPU's
/// view until `clear()` is called) and it backs the per-run `subData`
/// source pointer in `flushPendingSpawns()`.
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
        // Reserve worst-case capacity so push_back in the spawn hot path
        // never reallocates (per cpp-ecs.md "No allocations in hot tick
        // paths"). 4 bytes/slot × 4096 = 16 KiB upper bound.
        pendingIndices_.reserve(capacity_);
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
    /// from zero. As a side effect, sequential successful reservations
    /// produce sequential indices — `flushPendingSpawns()` relies on
    /// this to coalesce the per-frame upload into one `subData` call.
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

    /// Queue a write into a reserved slot. Mirrors into the CPU vector
    /// (so subsequent `reserveSlot` calls skip the slot) and records the
    /// index in `pendingIndices_`; the actual GPU upload happens in the
    /// next `flushPendingSpawns()` call, which is invoked once per frame
    /// from `UPDATE_GPU_PARTICLES` before the compute dispatch reads the
    /// SSBO. Batching matters on Metal: each `subData` orphans the whole
    /// MTL buffer, so per-spawn uploads dominate continuous-emitter
    /// workloads.
    void writeSlot(std::uint32_t index, const IRRender::GpuParticle &particle) {
        particles_[index] = particle;
        pendingIndices_.push_back(index);
    }

    /// Flush queued spawn writes to the GPU SSBO. Sorts and dedupes
    /// `pendingIndices_`, then issues one `Buffer::subData` per
    /// contiguous run of indices. Only the just-written slots are
    /// uploaded — never "untouched" interior slots, because their CPU
    /// mirror is stale relative to the GPU update kernel's running
    /// position/lifetime state.
    ///
    /// Empty-list fast path. Capacity of `pendingIndices_` is preserved
    /// across frames (clear() does not deallocate), so steady state
    /// performs no allocations.
    void flushPendingSpawns() {
        if (pendingIndices_.empty() || buffer_.second == nullptr) {
            return;
        }

        std::sort(pendingIndices_.begin(), pendingIndices_.end());
        pendingIndices_.erase(
            std::unique(pendingIndices_.begin(), pendingIndices_.end()),
            pendingIndices_.end()
        );

        constexpr std::size_t kStride = sizeof(IRRender::GpuParticle);
        std::uint32_t runStart = pendingIndices_.front();
        std::uint32_t runEnd = runStart;
        for (std::size_t i = 1; i < pendingIndices_.size(); ++i) {
            const std::uint32_t idx = pendingIndices_[i];
            if (idx == runEnd + 1u) {
                runEnd = idx;
                continue;
            }
            const std::size_t count = (runEnd - runStart) + 1u;
            buffer_.second->subData(
                static_cast<std::ptrdiff_t>(kStride * runStart),
                kStride * count,
                &particles_[runStart]
            );
            runStart = idx;
            runEnd = idx;
        }
        const std::size_t tailCount = (runEnd - runStart) + 1u;
        buffer_.second->subData(
            static_cast<std::ptrdiff_t>(kStride * runStart),
            kStride * tailCount,
            &particles_[runStart]
        );

        pendingIndices_.clear();
    }

    /// Mark every slot as dead on both CPU and GPU. Uploads the cleared
    /// CPU buffer to the SSBO in one `subData` call and drops any
    /// pending writes — caller-initiated reset, not a per-frame hot path.
    void clear() {
        for (auto &p : particles_) {
            p.lifetime_ = 0.0f;
        }
        nextSearchIndex_ = 0u;
        pendingIndices_.clear();
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
    std::vector<std::uint32_t> pendingIndices_;
};

} // namespace IRComponents

#endif /* COMPONENT_GPU_PARTICLE_POOL_H */
