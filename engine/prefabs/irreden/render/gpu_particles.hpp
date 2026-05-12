#ifndef IR_PREFAB_GPU_PARTICLES_H
#define IR_PREFAB_GPU_PARTICLES_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/components/component_gpu_particle_pool.hpp>

#include <cstdint>

namespace IRPrefab::GpuParticles {

using IRMath::Color;
using IRMath::vec3;

/// T-139 Phase 1 spawn API. Resolves the active canvas's `C_GPUParticlePool`,
/// reserves a slot, and writes a particle record. Lifetime is in seconds;
/// position and velocity are in voxel units (matching the rest of the
/// engine's world coordinates).
///
/// Returns the slot index on success, or `C_GPUParticlePool::kInvalidSlot`
/// if the active canvas has no particle pool or the pool is full. Phase 1
/// drops over-capacity spawns silently — over-allocation telemetry lives in
/// a follow-up phase.
inline std::uint32_t spawn(
    vec3 position,
    vec3 velocity,
    float lifetimeSeconds,
    Color color
) {
    using namespace IRComponents;
    const IREntity::EntityId canvas = IRRender::getActiveCanvasEntity();
    auto poolOpt = IREntity::getComponentOptional<C_GPUParticlePool>(canvas);
    if (!poolOpt.has_value()) {
        return C_GPUParticlePool::kInvalidSlot;
    }
    C_GPUParticlePool &pool = *poolOpt.value();
    const std::uint32_t slot = pool.reserveSlot();
    if (slot == C_GPUParticlePool::kInvalidSlot) {
        return slot;
    }
    pool.writeSlot(
        slot,
        IRRender::GpuParticle{position, lifetimeSeconds, velocity, color.toPackedRGBA()}
    );
    return slot;
}

/// Despawn every particle in the active canvas's pool. No-op if the canvas
/// has no pool component.
inline void clearAll() {
    using namespace IRComponents;
    const IREntity::EntityId canvas = IRRender::getActiveCanvasEntity();
    auto poolOpt = IREntity::getComponentOptional<C_GPUParticlePool>(canvas);
    if (!poolOpt.has_value()) {
        return;
    }
    poolOpt.value()->clear();
}

} // namespace IRPrefab::GpuParticles

#endif /* IR_PREFAB_GPU_PARTICLES_H */
