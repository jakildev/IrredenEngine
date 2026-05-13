#ifndef COMPONENT_STATELESS_PARTICLE_EMITTERS_H
#define COMPONENT_STATELESS_PARTICLE_EMITTERS_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/ir_render_types.hpp>

#include <cstdint>
#include <utility>
#include <vector>

using namespace IRMath;

namespace IRComponents {

/// T-163 Phase 1 — stateless particle emitter set, one component per canvas.
///
/// Each entry in `emitters_` is a fully self-describing emitter; the render
/// compute shader reconstructs every particle's position and color from
/// `(emitter, subIndex, currentTime)` via a closed-form gravity-with-jitter
/// trajectory, so there is no per-particle GPU state, no per-frame update
/// pass, and no spawn / despawn bookkeeping. Per-frame CPU work is
/// proportional to `emitterCount_` — a small `subData` for the per-frame
/// UBO header plus a per-emitter SSBO upload independent of particle count.
///
/// One pool per canvas — attach this component to the canvas entity you
/// want the particles rendered into. The render system reads the canvas's
/// own `C_TriangleCanvasTextures` because both components live on the
/// same archetype.
///
/// Storage split: per-frame header lives in a UBO (`frameBuffer_`); the
/// emitter descriptor array lives in an SSBO (`emitterBuffer_`). The SSBO
/// path keeps the array on `device` storage in Metal — straightforward and
/// free of the constant-storage layout flakiness observed with large
/// nested-struct arrays during Phase 1 bring-up. The pattern mirrors T-139
/// (SSBO for particle data, UBO for per-frame inputs) and follows
/// `.claude/rules/cpp-ecs.md` "No dirty flags on components" — push at
/// mutation time via `replaceEmitter` / `addEmitter`, with the system
/// driving per-frame upload of both buffers each tick.
struct C_StatelessParticleEmitters {
    C_StatelessParticleEmitters()
        : frameBuffer_{IRRender::createResource<IRRender::Buffer>(
              nullptr,
              sizeof(IRRender::FrameDataStatelessParticles),
              IRRender::BUFFER_STORAGE_DYNAMIC,
              IRRender::BufferTarget::UNIFORM,
              IRRender::kBufferIndex_FrameDataStatelessParticles
          )}
        , emitterBuffer_{IRRender::createResource<IRRender::Buffer>(
              nullptr,
              sizeof(IRRender::GpuParticleEmitter) * IRRender::kMaxStatelessEmitters,
              IRRender::BUFFER_STORAGE_DYNAMIC,
              IRRender::BufferTarget::SHADER_STORAGE,
              IRRender::kBufferIndex_StatelessParticleEmitters
          )} {
        emitters_.reserve(IRRender::kMaxStatelessEmitters);
    }

    void onDestroy() {
        IRRender::destroyResource<IRRender::Buffer>(frameBuffer_.first);
        IRRender::destroyResource<IRRender::Buffer>(emitterBuffer_.first);
    }

    /// Append an emitter to the set. Pushes the new slot to the GPU
    /// immediately via a one-slot `subData` (per `cpp-ecs.md` "No dirty
    /// flags" — mutation-time upload, not per-frame sync). Silently drops
    /// the request when the cap is reached. Returns the emitter index on
    /// success or `kInvalidEmitter` if full.
    std::uint32_t addEmitter(const IRRender::GpuParticleEmitter &emitter) {
        if (emitters_.size() >= IRRender::kMaxStatelessEmitters) {
            return kInvalidEmitter;
        }
        const std::uint32_t index = static_cast<std::uint32_t>(emitters_.size());
        emitters_.push_back(emitter);
        if (emitterBuffer_.second != nullptr) {
            emitterBuffer_.second->subData(
                static_cast<std::ptrdiff_t>(index) * sizeof(IRRender::GpuParticleEmitter),
                sizeof(IRRender::GpuParticleEmitter),
                &emitter
            );
        }
        return index;
    }

    /// Replace an existing emitter's descriptor in place. No-op when the
    /// index is out of range — fail-soft so a caller holding a stale index
    /// after `clear()` doesn't crash the frame. Pushes the changed slot to
    /// the GPU immediately.
    void replaceEmitter(std::uint32_t index, const IRRender::GpuParticleEmitter &emitter) {
        if (index >= emitters_.size()) {
            return;
        }
        emitters_[index] = emitter;
        if (emitterBuffer_.second != nullptr) {
            emitterBuffer_.second->subData(
                static_cast<std::ptrdiff_t>(index) * sizeof(IRRender::GpuParticleEmitter),
                sizeof(IRRender::GpuParticleEmitter),
                &emitter
            );
        }
    }

    /// Drop every emitter; the next frame renders nothing until callers
    /// repopulate. Leaves the SSBO contents in place — the render shader
    /// gates on `emitterCount_` from the per-frame UBO header.
    void clear() { emitters_.clear(); }

    std::uint32_t emitterCount() const {
        return static_cast<std::uint32_t>(emitters_.size());
    }

    static constexpr std::uint32_t kInvalidEmitter = static_cast<std::uint32_t>(-1);

    std::vector<IRRender::GpuParticleEmitter> emitters_{};
    std::pair<IRRender::ResourceId, IRRender::Buffer *> frameBuffer_{0u, nullptr};
    std::pair<IRRender::ResourceId, IRRender::Buffer *> emitterBuffer_{0u, nullptr};
};

} // namespace IRComponents

#endif /* COMPONENT_STATELESS_PARTICLE_EMITTERS_H */
