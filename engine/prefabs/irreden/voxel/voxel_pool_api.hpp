#ifndef IR_PREFAB_VOXEL_POOL_API_H
#define IR_PREFAB_VOXEL_POOL_API_H

// Prefab-scoped façade over the render-side voxel pool API.
//
// Lets pool-owning components (notably `C_VoxelSetNew`) drop their direct
// `<irreden/ir_render.hpp>` and `<irreden/render/texture.hpp>` includes —
// the call surface they need (allocate / deallocate / active canvas lookup)
// is re-exposed here under `IRPrefab::VoxelPool::*` so the public component
// header stays render-neutral. Implementation forwarders are inline and
// route into `IRRender::*` directly, preserving the existing performance
// contract from `C_VoxelPool::allocateVoxels` (single canvas-map lookup,
// no virtual indirection, no per-call hash beyond what's already there).
//
// Layering motivation: `engine/script/` consumers (prefab_api.cpp et al.)
// transitively include this header through component_voxel_set.hpp; keeping
// `<irreden/ir_render.hpp>` out of the component's public surface concentrates
// the render dependency in this one shim header — see
// `engine/script/CLAUDE.md` for the T-201 layering plan.

#include <irreden/ir_render.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/voxel_pool_allocation.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <cstddef>
#include <string>

namespace IRPrefab::VoxelPool {

// Entity id of the currently active render canvas. Mirrors
// `IRRender::getActiveCanvasEntity` semantics: asserts when no render
// manager exists. Pool-owning components capture this at ctor time so
// later operations don't need to re-look-up the canvas.
inline IREntity::EntityId activeCanvasEntity() {
    return IRRender::getActiveCanvasEntity();
}

// Active-canvas snapshot that returns `kNullEntity` when no render manager
// exists. Use this from headless / pre-canvas construction paths (asset
// tooling, prefab spawn in tests) where the asserting variant would abort.
inline IREntity::EntityId activeCanvasEntityOrNull() {
    return IRRender::getActiveCanvasEntityOrNull();
}

// Allocate a contiguous span of @p size voxels from the named canvas pool.
// The returned `VoxelPoolAllocation::startIndex_` is the source of truth
// for the span's position inside the pool's underlying voxel arrays —
// never recompute it from `positions.data() - basePtr`.
inline IRRender::VoxelPoolAllocation
allocate(unsigned int size, const std::string &canvasName = "main") {
    return IRRender::allocateVoxels(size, canvasName);
}

// Release a previously-allocated voxel span back to the pool. Pass the
// start index returned by `allocate` and the same size.
inline void
deallocate(std::size_t startIndex, std::size_t size, const std::string &canvasName = "main") {
    IRRender::deallocateVoxels(startIndex, size, canvasName);
}

// Push-at-mutation routes that keep the GPU active-slot bitmask
// (`C_VoxelPool::m_activeMask` — read by `c_voxel_visibility_compact`)
// in sync with the color span the caller just wrote. `C_VoxelSetNew`'s
// span-direct mutators (ctors, `changeVoxelColor`, `deactivateAll`,
// `activateAll`, `fillPlane`, `reshape`) call into these immediately
// after the color write so the next render-frame upload sees a
// consistent mask without any dirty flag.
inline void
markRangeActive(std::size_t startIndex, std::size_t count, const std::string &canvasName = "main") {
    IRRender::markVoxelPoolRangeActive(startIndex, count, canvasName);
}

inline void markRangeInactive(
    std::size_t startIndex, std::size_t count, const std::string &canvasName = "main"
) {
    IRRender::markVoxelPoolRangeInactive(startIndex, count, canvasName);
}

inline void markVoxelActive(
    std::size_t startIndex,
    std::size_t voxelIdx,
    bool active,
    const std::string &canvasName = "main"
) {
    IRRender::markVoxelPoolVoxelActive(startIndex + voxelIdx, active, canvasName);
}

inline void resyncRangeFromColors(
    std::size_t startIndex, std::size_t count, const std::string &canvasName = "main"
) {
    IRRender::resyncVoxelPoolRangeFromColors(startIndex, count, canvasName);
}

namespace detail {

// Resolve the C_VoxelPool owned by a canvas entity, or nullptr when the
// entity is null / destroyed / has no pool. The entity-keyed pool ops
// below route through this so any canvas that owns a pool — including a
// detached entity's per-entity canvas — is a valid target, without
// needing a RenderManager canvas-name-map entry.
inline IRComponents::C_VoxelPool *poolForCanvas(IREntity::EntityId canvasEntity) {
    if (canvasEntity == IREntity::kNullEntity || !IREntity::entityExists(canvasEntity)) {
        return nullptr;
    }
    auto poolOpt = IREntity::getComponentOptional<IRComponents::C_VoxelPool>(canvasEntity);
    return poolOpt.has_value() ? poolOpt.value() : nullptr;
}

} // namespace detail

// Entity-keyed pool ops. The name-keyed forms above resolve through
// RenderManager's canvas-name map, which only carries the ctor-time
// "main" / "background" / "gui" canvases. These forms take the canvas
// entity directly, so a detached entity's per-entity canvas can own and
// grow its own voxel pool. `C_VoxelSetNew` captures its target canvas in
// `canvasEntity_` and routes every pool op through these.
inline IRRender::VoxelPoolAllocation allocate(unsigned int size, IREntity::EntityId canvasEntity) {
    if (auto *pool = detail::poolForCanvas(canvasEntity)) {
        return pool->allocateVoxels(size);
    }
    return IRRender::VoxelPoolAllocation{};
}

inline void deallocate(std::size_t startIndex, std::size_t count, IREntity::EntityId canvasEntity) {
    if (auto *pool = detail::poolForCanvas(canvasEntity)) {
        pool->deallocateVoxels(startIndex, count);
    }
}

inline void
markRangeActive(std::size_t startIndex, std::size_t count, IREntity::EntityId canvasEntity) {
    if (auto *pool = detail::poolForCanvas(canvasEntity)) {
        pool->setActiveMaskRange(startIndex, count);
    }
}

inline void
markRangeInactive(std::size_t startIndex, std::size_t count, IREntity::EntityId canvasEntity) {
    if (auto *pool = detail::poolForCanvas(canvasEntity)) {
        pool->clearActiveMaskRange(startIndex, count);
    }
}

inline void markVoxelActive(
    std::size_t startIndex, std::size_t voxelIdx, bool active, IREntity::EntityId canvasEntity
) {
    if (auto *pool = detail::poolForCanvas(canvasEntity)) {
        if (active) {
            pool->setActiveBit(startIndex + voxelIdx);
        } else {
            pool->clearActiveBit(startIndex + voxelIdx);
        }
    }
}

inline void
resyncRangeFromColors(std::size_t startIndex, std::size_t count, IREntity::EntityId canvasEntity) {
    if (auto *pool = detail::poolForCanvas(canvasEntity)) {
        pool->resyncActiveMaskFromColors(startIndex, count);
    }
}

// Queue a voxel range for GPU position upload on the next
// VOXEL_TO_TRIXEL_STAGE_1 flush (mirrors the pending-range flush the per-frame
// UPDATE_VOXEL_SET_CHILDREN uses). Used by the post-load canvas-attach seed
// pass (`C_VoxelSetNew::attachToCanvas`, #2217) so a just-seeded set's local
// positions reach binding 5 on the first post-load frame.
inline void
queuePositionRange(std::size_t startIndex, std::size_t count, IREntity::EntityId canvasEntity) {
    if (auto *pool = detail::poolForCanvas(canvasEntity)) {
        pool->queuePositionRange(startIndex, count);
    }
}

// Push-at-mutation route for the per-trixel-priority aggregate (#2155). The
// C_VoxelSetNew priority mutators call this with the delta of priority-carrying
// voxels they just added (+) or removed (-) so the pool's count — read once per
// frame by VOXEL_TO_TRIXEL_STAGE_1 to gate the finalization shader's entity-id
// decode read — stays current without a per-voxel scan.
inline void adjustPerTrixelPriorityVoxelCount(int delta, IREntity::EntityId canvasEntity) {
    if (delta == 0) {
        return;
    }
    if (auto *pool = detail::poolForCanvas(canvasEntity)) {
        pool->adjustPerTrixelPriorityVoxelCount(delta);
    }
}

// True when @p canvasEntity owns a live C_VoxelPool (exists, not destroyed,
// carries the component). The post-load canvas-attach seed pass
// (`C_VoxelSetNew::attachToCanvas`) uses this to resolve a seedable target
// before it moves its staged data — seeding into a null pool would discard it.
inline bool hasPool(IREntity::EntityId canvasEntity) {
    return detail::poolForCanvas(canvasEntity) != nullptr;
}

// Performs a single canvas-entity lookup and calls fn(C_VoxelPool&).
// Prefer this over calling markVoxelActive N times to avoid N RenderManager
// lookups; fillPlane-style loops that activate many individual slots benefit
// from calling pool.setActiveBit(absIdx) directly on the resolved pool.
template <typename Fn> inline void withPoolByEntity(IREntity::EntityId canvasEntity, Fn &&fn) {
    if (auto *pool = detail::poolForCanvas(canvasEntity)) {
        fn(*pool);
    }
}

} // namespace IRPrefab::VoxelPool

#endif /* IR_PREFAB_VOXEL_POOL_API_H */
