#ifndef IR_PREFAB_ROTATION_MODE_H
#define IR_PREFAB_ROTATION_MODE_H

// Prefab-scoped helpers for `C_RotationMode`. The component itself is
// plain data; cross-entity orchestration — allocating the per-entity
// canvas on DETACHED, destroying it on GRID — lives here so the
// component layout stays trivial and archetype-iteration friendly.
//
// Spawn-time mode selection is handled by `IRPrefab::Prefab::spawnPrefab`
// directly. Use `setMode` to change an already-spawned entity's mode at
// runtime; it preserves the rest of the entity's components and pays
// the re-allocation cost (one canvas-entity create or destroy) inline.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/entity_canvas.hpp>
#include <irreden/render/systems/system_update_voxel_positions_gpu.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace IRPrefab::RotationMode {

/// Transition an entity to `newMode`, allocating or destroying its
/// per-entity canvas as required.
///
/// - GRID → DETACHED: allocates a child canvas via
///   `IRPrefab::EntityCanvas::create(canvasName, canvasSize)` and
///   attaches `C_EntityCanvas` to `entity`. The previous canvas (if
///   any) is left alone — re-entering DETACHED from DETACHED is a
///   no-op rather than a re-allocation churn.
/// - DETACHED → GRID: destroys the entity's `C_EntityCanvas` child
///   entity (freeing its GPU textures via `onDestroy`) and removes
///   the component from `entity`.
/// - Same mode in/out: no-op.
///
/// `canvasName` and `canvasSize` are only consulted on a GRID→DETACHED
/// transition; pass sensible defaults otherwise.
namespace detail {

// Enter MAIN_CANVAS_SO3 (#1299, PR-A): acquire a GPU transform slot, route the
// entity's voxel range through the prepass, and opt it into the octahedral snap.
// The entity stays on the shared main canvas — no child canvas. Degrades to a
// warning + no-op when the entity has no voxel set or the slot allocator was
// never wired (`IRPrefab::VoxelTransform::setAllocatorSystem`).
inline void setupMainCanvasSO3(IREntity::EntityId entity) {
    using IRComponents::C_VoxelPool;
    using IRComponents::C_VoxelSetNew;
    auto vsOpt = IREntity::getComponentOptional<C_VoxelSetNew>(entity);
    if (!vsOpt) {
        IR_LOG_WARN(
            "setMode(MAIN_CANVAS_SO3): entity {} has no C_VoxelSetNew; mode set "
            "but no GPU transform applied",
            entity
        );
        return;
    }
    C_VoxelSetNew &vs = *vsOpt.value();
    const std::uint32_t slot = IRPrefab::VoxelTransform::acquireSlot();
    if (slot == IRRender::kVoxelTransformStatic) {
        IR_LOG_WARN(
            "setMode(MAIN_CANVAS_SO3): no transform slot for entity {} — wire "
            "IRPrefab::VoxelTransform::setAllocatorSystem(updateVoxelPositionsId) "
            "at init (or the slot budget is exhausted); set stays CPU-direct",
            entity
        );
        return;
    }
    vs.gpuTransformSlot_ = slot;
    vs.snapTransformOctahedral_ = true;
    if (vs.canvasEntity_ != IREntity::kNullEntity && vs.numVoxels_ > 0) {
        C_VoxelPool &pool = IREntity::getComponent<C_VoxelPool>(vs.canvasEntity_);
        pool.setTransformIndexForRange(
            vs.voxelStartIdx_, static_cast<std::size_t>(vs.numVoxels_), slot
        );
        pool.incrementSO3SetCount();
    }
}

// Leave MAIN_CANVAS_SO3: release the slot, reset the set back to CPU-direct,
// clear the per-voxel triplet stamp, and decrement the canvas SO(3) counter.
inline void teardownMainCanvasSO3(IREntity::EntityId entity) {
    using IRComponents::C_VoxelPool;
    using IRComponents::C_VoxelSetNew;
    auto vsOpt = IREntity::getComponentOptional<C_VoxelSetNew>(entity);
    if (!vsOpt) {
        return;
    }
    C_VoxelSetNew &vs = *vsOpt.value();
    if (vs.canvasEntity_ != IREntity::kNullEntity && vs.numVoxels_ > 0) {
        C_VoxelPool &pool = IREntity::getComponent<C_VoxelPool>(vs.canvasEntity_);
        pool.setTransformIndexForRange(
            vs.voxelStartIdx_,
            static_cast<std::size_t>(vs.numVoxels_),
            IRRender::kVoxelTransformStatic
        );
        pool.setVoxelReservedForRange(
            vs.voxelStartIdx_, static_cast<std::size_t>(vs.numVoxels_), 0u
        );
        pool.decrementSO3SetCount();
    }
    IRPrefab::VoxelTransform::releaseSlot(vs.gpuTransformSlot_);
    vs.gpuTransformSlot_ = IRRender::kVoxelTransformStatic;
    vs.snapTransformOctahedral_ = false;
}

} // namespace detail

inline void setMode(
    IREntity::EntityId entity,
    IRComponents::RotationMode newMode,
    std::string canvasName = {},
    IRMath::ivec2 canvasSize = IRMath::ivec2{0}
) {
    using IRComponents::C_EntityCanvas;
    using IRComponents::C_RotationMode;
    using IRComponents::RotationMode;

    auto modeOpt = IREntity::getComponentOptional<C_RotationMode>(entity);
    const RotationMode current = modeOpt ? modeOpt.value()->mode_ : RotationMode::GRID;
    if (current == newMode) {
        return;
    }

    // Exit the current mode's resources first (symmetric teardown), then enter
    // the new mode. Each mode owns a distinct resource: DETACHED a child canvas,
    // MAIN_CANVAS_SO3 a GPU transform slot; GRID owns neither.
    if (current == RotationMode::DETACHED) {
        auto existing = IREntity::getComponentOptional<C_EntityCanvas>(entity);
        if (existing) {
            const IREntity::EntityId canvas = existing.value()->canvasEntity_;
            if (canvas != IREntity::kNullEntity) {
                IREntity::destroyEntity(canvas);
            }
            IREntity::removeComponent<C_EntityCanvas>(entity);
        }
    } else if (current == RotationMode::MAIN_CANVAS_SO3) {
        detail::teardownMainCanvasSO3(entity);
    }

    if (newMode == RotationMode::DETACHED) {
        IR_ASSERT(
            IRRender::g_renderManager != nullptr,
            "setMode(DETACHED) requires a live RenderManager"
        );
        auto existing = IREntity::getComponentOptional<C_EntityCanvas>(entity);
        if (!existing) {
            IREntity::setComponent(entity, IRPrefab::EntityCanvas::create(canvasName, canvasSize));
        }
    } else if (newMode == RotationMode::MAIN_CANVAS_SO3) {
        IR_ASSERT(
            IRRender::g_renderManager != nullptr,
            "setMode(MAIN_CANVAS_SO3) requires a live RenderManager"
        );
        detail::setupMainCanvasSO3(entity);
    }
    // GRID: no entry work — the exit branch above already released resources.

    IREntity::setComponent(entity, C_RotationMode{newMode});
}

} // namespace IRPrefab::RotationMode

#endif /* IR_PREFAB_ROTATION_MODE_H */
