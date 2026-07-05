#ifndef COMPONENT_VOXEL_SET_H
#define COMPONENT_VOXEL_SET_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/face_occupancy.hpp>
#include <irreden/voxel/voxel_pool_api.hpp>

#include <vector>

using namespace IRMath;
// TODO: add primitives to voxel set, not just setting individual voxels...
// UPDATE: see component_geometric_shape.hpp

namespace IRComponents {

// Allocates a contiguous span of voxels in the canvas VoxelPool.
// Prefer C_ShapeDescriptor for entities whose visual is a geometric shape
// (box, sphere, etc.) -- it avoids per-voxel allocation entirely by
// evaluating SDFs on the GPU. Reserve C_VoxelSetNew for cases that need
// individual voxel colors (imported voxel art, terrain editing, particles).
struct C_VoxelSetNew {
    int numVoxels_;
    ivec3 size_;
    IREntity::EntityId canvasEntity_ = IREntity::kNullEntity;
    // TODO: Evaulate if we should store here or somewhere else.
    IREntity::EntityId ownerEntityId_ = IREntity::kNullEntity;

    // Index into the canvas voxel pool's underlying arrays. Captured at
    // allocation time so consumers never recompute it from a pointer-diff
    // against a separately-cached @c C_VoxelPool* (a stale cached pool
    // pointer made the diff resolve to a wild index).
    size_t voxelStartIdx_ = 0;

    // Number of this set's voxels currently carrying a non-zero per-trixel
    // priority (#2155). Maintained by changeVoxelPriority / changeVoxelPriorityAll;
    // each change also pushes its delta to the owning pool's aggregate
    // (IRPrefab::VoxelPool::adjustPerTrixelPriorityVoxelCount) so the pool can
    // report — once per frame, no per-voxel scan — whether the finalization shader
    // must decode the entity-id carrier. Released back to the pool in onDestroy().
    std::uint32_t perTrixelPriorityVoxelCount_ = 0;

    // GPU transform-indirection slot for this set (#1396). `kVoxelTransformStatic`
    // (the default) keeps the set on the CPU-direct world-position path:
    // UPDATE_VOXEL_SET_CHILDREN folds the parent translation in and uploads
    // binding 5 directly. Any other value routes the set through the GPU
    // voxel-position prepass — its voxels carry this slot as their per-voxel
    // transform index, and UPDATE_VOXEL_POSITIONS_GPU writes
    // EntityTransformBuffer[slot] from the entity's C_WorldTransform each frame.
    std::uint32_t gpuTransformSlot_ = IRRender::kVoxelTransformStatic;

    // Local voxel position (16-byte GPU stride POD; see VoxelGpuPosition).
    std::span<IRRender::VoxelGpuPosition> positions_;

    // Per-voxel deformation offset authored by VOXEL_SQUASH_STRETCH.
    // CPU-only scratch — summed into the world position by
    // UPDATE_VOXEL_SET_CHILDREN, never uploaded directly to the GPU.
    std::span<vec3> positionOffsets_;

    // World voxel position recalculated each update. Same 16-byte stride
    // as positions_ so the std430 SSBO upload stride matches the GPU
    // contract in c_voxel_to_trixel_stage_1.
    std::span<IRRender::VoxelGpuPosition> globalPositions_;

    std::span<C_Voxel> voxels_;

    // Headless / pre-canvas staging. Populated by the dense-data ctor
    // when no render canvas is active at construction time (tests,
    // asset-only tooling, prefab spawn before a canvas exists). When
    // present, `numVoxels_` is 0 and the pool spans above are empty —
    // the canonical data lives here and `recordCount()` reflects it.
    // A future canvas-attach pass moves these into the pool span,
    // then clears the staging vector.
    std::vector<C_Voxel> pendingVoxels_;

    // Origin of `pendingVoxels_` data in voxel-grid space, i.e. the
    // value of `IRAsset::DenseVoxelSet::boundsMin_`. The staged
    // entry at flat index i corresponds to grid position
    // `pendingBoundsMin_ + index1DtoIndex3D(i, size_)`. Only
    // meaningful when `pendingVoxels_` is non-empty.
    ivec3 pendingBoundsMin_ = ivec3(0);

    // Authored-source snapshot for the GRID inverse re-voxelize (#1720) —
    // the CPU analog of the detached path's `C_DetachedRevoxelizeBuffer::
    // sourceGrid_`. While a GRID-mode set rotates, REBUILD_GRID_VOXELS
    // re-arranges the pool span per frame (slot i becomes "dest cell i", and
    // colors are duplicated across slots wherever one source voxel covers
    // several dest cells), so the pool colors stop being the authored truth.
    // This vector holds that truth: built lazily by the rebuild system on the
    // first non-identity frame (a copy of the span's colors), consumed every
    // rotating frame, and cleared again on the next identity frame after the
    // system restores the span — so its non-emptiness IS the "span is in a
    // re-voxelized arrangement" state, with no separate flag to drift.
    // The color mutators below mirror writes into it while it exists so a
    // mutation during a spin survives the per-frame re-derivation. Raw
    // `voxels_` span writes (the SDF-carve pattern) bypass the mirror — they
    // are only valid on a set that has not begun GRID rotation (carve at
    // creation time, before the first rotated frame), same window in which
    // `syncActiveMask()` is honest.
    std::vector<C_Voxel> rotationSourceVoxels_;

    // `targetCanvas` selects which canvas's voxel pool this set allocates
    // from. `kNullEntity` (the default) keeps the historical behavior —
    // the currently-active canvas, normally "main". Pass a detached
    // entity's per-entity canvas to render this set into that canvas.
    C_VoxelSetNew(
        ivec3 size,
        Color color = IRColors::kGreen,
        bool centerAroundOrigin = false,
        IREntity::EntityId targetCanvas = IREntity::kNullEntity
    )
        : numVoxels_{size.x * size.y * size.z}
        , size_{size} {
        const int requestedVoxels = size.x * size.y * size.z;
        canvasEntity_ = targetCanvas != IREntity::kNullEntity
                            ? targetCanvas
                            : IRPrefab::VoxelPool::activeCanvasEntity();
        auto allocation = IRPrefab::VoxelPool::allocate(requestedVoxels, canvasEntity_);
        voxelStartIdx_ = allocation.startIndex_;
        positions_ = allocation.positions_;
        positionOffsets_ = allocation.positionOffsets_;
        globalPositions_ = allocation.positionGlobals_;
        voxels_ = allocation.voxels_;

        // Keep runtime-safe bounds even if allocation returns an unexpected span size.
        numVoxels_ = static_cast<int>(IRMath::min(
            IRMath::min(positions_.size(), positionOffsets_.size()),
            IRMath::min(globalPositions_.size(), voxels_.size())
        ));
        if (numVoxels_ != requestedVoxels) {
            IRE_LOG_ERROR(
                "VoxelSet allocation mismatch: requested={}, positions={}, offsets={}, "
                "globals={}, colors={}",
                requestedVoxels,
                positions_.size(),
                positionOffsets_.size(),
                globalPositions_.size(),
                voxels_.size()
            );
            // Release whatever the allocator handed back — `numVoxels_` is
            // the min-span count, which on today's allocator either equals
            // `requestedVoxels` (no mismatch, branch not taken) or is 0
            // (out-of-voxels assert fall-through, no slots were reserved
            // and this is a no-op). The dealloc is kept for symmetry with
            // a hypothetical future allocator that returns partial spans.
            // Zeroing `numVoxels_` then keeps `onDestroy()`'s guard correct.
            IRPrefab::VoxelPool::deallocate(
                voxelStartIdx_,
                static_cast<size_t>(numVoxels_),
                canvasEntity_
            );
            numVoxels_ = 0;
            size_ = ivec3(0);
            return;
        }

        vec3 offset = centerAroundOrigin
                          ? vec3(-(size.x - 1) * 0.5f, -(size.y - 1) * 0.5f, -(size.z - 1) * 0.5f)
                          : vec3(0.0f);
        for (int x = 0; x < size.x; x++) {
            for (int y = 0; y < size.y; y++) {
                for (int z = 0; z < size.z; z++) {
                    vec3 pos = vec3(x, y, z) + offset;
                    positions_[index3DtoIndex1D(ivec3(x, y, z), size)] =
                        IRRender::VoxelGpuPosition{pos, 0.0f};
                    voxels_[index3DtoIndex1D(ivec3(x, y, z), size)].color_ = color;
                }
            }
        }
        // Default `color` is opaque (alpha=255 from `IRMath::Color`'s ctor),
        // so the fast-path is `markRangeActive`. The fallback handles a caller
        // who passes a transparent color.
        if (color.alpha_ != 0) {
            IRPrefab::VoxelPool::markRangeActive(voxelStartIdx_, numVoxels_, canvasEntity_);
        } else {
            IRPrefab::VoxelPool::markRangeInactive(voxelStartIdx_, numVoxels_, canvasEntity_);
        }
        IRPrefab::Voxel::recomputeFaceOccupancy(voxels_, size_);
        IRE_LOG_DEBUG("Allocated {} voxel(s)", numVoxels_);
    }

    C_VoxelSetNew(int width, int height, int depth)
        : C_VoxelSetNew(ivec3(width, height, depth)) {}

    C_VoxelSetNew(int width, int height, int depth, Color color)
        : C_VoxelSetNew(ivec3(width, height, depth), color) {}

    // default constructor — headless-safe, zero pool interaction. Produces an
    // empty placeholder set (numVoxels_ == 0, no canvas captured). It must NOT
    // allocate: the world-snapshot loader default-constructs a C_VoxelSetNew
    // inside `Result<C_VoxelSetNew>` while decoding (including the mutation-free
    // validate pass), so a pool-touching default ctor would both break the
    // "zero world mutation on error" contract and assert headlessly (no render
    // manager). The allocating element-count ctor is `C_VoxelSetNew(ivec3)`.
    C_VoxelSetNew()
        : numVoxels_{0}
        , size_{ivec3(0, 0, 0)} {}

    // Tag selecting the zero-pool staged constructor below.
    struct StagedInit {};

    // Construct directly in staged mode with NO pool interaction. The load
    // path (`SaveSerialize<C_VoxelSetNew>::read`) uses this so deserialization
    // — including the loader's mutation-free validate pass
    // (`world_snapshot.cpp` phase 2b, which dry-runs `read`) — never allocates
    // a pool span. `attachToCanvas` moves the staged data into a live span once
    // a render context exists (#2217, W-10). The GPU transform slot is not
    // persisted; a reloaded non-static set re-registers a fresh slot lazily.
    C_VoxelSetNew(
        StagedInit,
        ivec3 size,
        ivec3 boundsMin,
        std::vector<C_Voxel> voxels,
        IREntity::EntityId canvasEntity
    )
        : numVoxels_{0}
        , size_{size}
        , canvasEntity_{canvasEntity}
        , pendingVoxels_{std::move(voxels)}
        , pendingBoundsMin_{boundsMin} {}

    // Dense-data ctor for Prefab.spawn — headless-safe: stages in
    // `pendingVoxels_` without a canvas, allocates from the pool with one.
    // Bounds + ordering semantics live in `voxel/CLAUDE.md` "C_VoxelSetNew
    // headless / staged mode" and `voxel/dense_bridge.hpp`.
    C_VoxelSetNew(
        ivec3 boundsMin,
        ivec3 boundsMax,
        std::span<const C_Voxel> voxels,
        IREntity::EntityId targetCanvas = IREntity::kNullEntity
    )
        : numVoxels_{0}
        , size_{boundsMax - boundsMin} {
        canvasEntity_ = targetCanvas != IREntity::kNullEntity
                            ? targetCanvas
                            : IRPrefab::VoxelPool::activeCanvasEntityOrNull();
        const ivec3 extent = size_;
        const std::size_t requestedVoxels = (extent.x > 0 && extent.y > 0 && extent.z > 0)
                                                ? static_cast<std::size_t>(extent.x) *
                                                      static_cast<std::size_t>(extent.y) *
                                                      static_cast<std::size_t>(extent.z)
                                                : 0u;
        if (requestedVoxels == 0u || voxels.size() != requestedVoxels) {
            // Empty or mismatched payload — leave the set empty and let
            // the caller diagnose via `recordCount()`. Avoids allocating
            // pool slots we can't faithfully populate.
            size_ = ivec3(0);
            return;
        }

        if (canvasEntity_ == IREntity::kNullEntity) {
            // Headless / pre-canvas staging path.
            pendingVoxels_.assign(voxels.begin(), voxels.end());
            pendingBoundsMin_ = boundsMin;
            return;
        }

        seedIntoPool(boundsMin, voxels, canvasEntity_);
        IRE_LOG_DEBUG("Allocated {} dense voxel(s) from voxel_ref", numVoxels_);
    }

    std::size_t recordCount() const {
        return pendingVoxels_.empty() ? static_cast<std::size_t>(numVoxels_)
                                      : pendingVoxels_.size();
    }

    // TODO: should a similar onCreate method be used for allocating
    // voxels, just in case the constructor might be called in more than
    // one place?
    void onDestroy() {
        // `numVoxels_ > 0` iff a pool allocation succeeded and was fully
        // populated. Both ctors' mismatch paths deallocate the reservation
        // inline and zero `numVoxels_`, and the headless staging path
        // never touches the pool — so this guard skips exactly the cases
        // that have nothing to release.
        if (numVoxels_ > 0) {
            // Release this set's per-trixel-priority contribution (#2155) before
            // the span goes back to the pool, so a canvas whose only priority
            // voxels lived in a destroyed set drops back to the fast path.
            if (perTrixelPriorityVoxelCount_ > 0) {
                IRPrefab::VoxelPool::adjustPerTrixelPriorityVoxelCount(
                    -static_cast<int>(perTrixelPriorityVoxelCount_),
                    canvasEntity_
                );
                perTrixelPriorityVoxelCount_ = 0;
            }
            IRPrefab::VoxelPool::deallocate(
                voxelStartIdx_,
                static_cast<size_t>(numVoxels_),
                canvasEntity_
            );
            IRE_LOG_DEBUG("Deallocated {} voxels", numVoxels_);
        }
    }

    // Mirror one slot's record into the rotation-source snapshot, if it
    // exists (see `rotationSourceVoxels_` — keeps mutations made during a
    // GRID spin from being overwritten by the per-frame re-voxelize).
    void mirrorToRotationSource(int idx) {
        if (static_cast<std::size_t>(idx) < rotationSourceVoxels_.size()) {
            rotationSourceVoxels_[idx] = voxels_[idx];
        }
    }

    void changeVoxelColor(ivec3 index, Color color) {
        const int idx = index3DtoIndex1D(index, size_);
        voxels_[idx].color_ = color;
        mirrorToRotationSource(idx);
        IRPrefab::VoxelPool::markVoxelActive(voxelStartIdx_, idx, color.alpha_ != 0, canvasEntity_);
    }

    void changeVoxelColorAll(Color color) {
        for (int i = 0; i < numVoxels_; i++) {
            voxels_[i].color_ = color;
            mirrorToRotationSource(i);
        }
        if (color.alpha_ != 0) {
            IRPrefab::VoxelPool::markRangeActive(voxelStartIdx_, numVoxels_, canvasEntity_);
        } else {
            IRPrefab::VoxelPool::markRangeInactive(voxelStartIdx_, numVoxels_, canvasEntity_);
        }
    }

    // Per-trixel render priority (#1960). Sets the low 2 bits of the voxel's
    // `reserved_` carrier — 0 = default world tier, higher = renders in front of
    // lower tiers regardless of depth. Rides the per-frame Voxel-record (binding 6)
    // upload, so no active-mask change is needed; the stage-2 raster packs it into
    // the entity-id carrier and f_trixel_to_framebuffer resolves
    // tier = max(perEntityTier, perTrixelTier). Default 0 ⇒ byte-identical.
    void changeVoxelPriority(ivec3 index, std::uint8_t priority) {
        const int idx = index3DtoIndex1D(index, size_);
        const bool wasSet = (voxels_[idx].reserved_ & 0x3u) != 0u;
        voxels_[idx].reserved_ = (voxels_[idx].reserved_ & ~0x3u) | (priority & 0x3u);
        const bool nowSet = (priority & 0x3u) != 0u;
        if (nowSet != wasSet) {
            const int delta = nowSet ? 1 : -1;
            perTrixelPriorityVoxelCount_ =
                static_cast<std::uint32_t>(static_cast<int>(perTrixelPriorityVoxelCount_) + delta);
            IRPrefab::VoxelPool::adjustPerTrixelPriorityVoxelCount(delta, canvasEntity_);
        }
        mirrorToRotationSource(idx);
    }

    void changeVoxelPriorityAll(std::uint8_t priority) {
        for (int i = 0; i < numVoxels_; i++) {
            voxels_[i].reserved_ = (voxels_[i].reserved_ & ~0x3u) | (priority & 0x3u);
            mirrorToRotationSource(i);
        }
        // Conservative-TRUE: counts every slot (active or not) — an inactive
        // priority voxel can't render, so an over-count only costs the fast path.
        const std::uint32_t newCount =
            (priority & 0x3u) != 0u ? static_cast<std::uint32_t>(numVoxels_) : 0u;
        const int delta =
            static_cast<int>(newCount) - static_cast<int>(perTrixelPriorityVoxelCount_);
        IRPrefab::VoxelPool::adjustPerTrixelPriorityVoxelCount(delta, canvasEntity_);
        perTrixelPriorityVoxelCount_ = newCount;
    }

    void deactivateAll() {
        for (int i = 0; i < numVoxels_; i++) {
            voxels_[i].deactivate();
            mirrorToRotationSource(i);
        }
        IRPrefab::VoxelPool::markRangeInactive(voxelStartIdx_, numVoxels_, canvasEntity_);
        IRPrefab::Voxel::recomputeFaceOccupancy(voxels_, size_);
    }

    void activateAll() {
        for (int i = 0; i < numVoxels_; i++) {
            voxels_[i].activate();
            mirrorToRotationSource(i);
        }
        IRPrefab::VoxelPool::markRangeActive(voxelStartIdx_, numVoxels_, canvasEntity_);
        IRPrefab::Voxel::recomputeFaceOccupancy(voxels_, size_);
    }

    // Activates all voxels in the plane perpendicular to `axis` at `planeIndex`
    // and sets their color. Pair with deactivateAll() to seed an editor scene.
    // axis: 0=X, 1=Y, 2=Z. planeIndex must be in [0, size_[axis]).
    void fillPlane(int axis, int planeIndex, Color color) {
        const ivec3 sz = size_;
        auto [dim0, dim1] = IRMath::perpendicularAxes(axis);
        IRPrefab::VoxelPool::withPoolByEntity(canvasEntity_, [&](IRComponents::C_VoxelPool &pool) {
            for (int a = 0; a < sz[dim0]; ++a) {
                for (int b = 0; b < sz[dim1]; ++b) {
                    ivec3 coord{0, 0, 0};
                    coord[axis] = planeIndex;
                    coord[dim0] = a;
                    coord[dim1] = b;
                    const int idx = IRMath::index3DtoIndex1D(coord, sz);
                    voxels_[idx].color_ = color;
                    voxels_[idx].activate();
                    mirrorToRotationSource(idx);
                    pool.setActiveBit(voxelStartIdx_ + idx);
                }
            }
        });
        IRPrefab::Voxel::recomputeFaceOccupancy(voxels_, sz);
    }

    // take positions of all voxels in voxel object and form a new shape. This could
    // mean moving the parent positions of the Entity to the new desired location OMG

    // void reform(std::vector<EntityHandle>& voxelSetEntities) {
    //     for(auto& voxel: voxelSetEntities) {

    //     }
    // }

    // Be able to bind a function like this to a command!
    void reshape(Shape3D shape3D) {
        if (shape3D == Shape3D::RECTANGULAR_PRISM) {
            for (int x = 0; x < size_.x; x++) {
                for (int y = 0; y < size_.y; y++) {
                    for (int z = 0; z < size_.z; z++) {
                        int index = index3DtoIndex1D(ivec3(x, y, z), size_);
                        voxels_[index].activate();
                        mirrorToRotationSource(index);
                    }
                }
            }
            IRPrefab::VoxelPool::markRangeActive(voxelStartIdx_, numVoxels_, canvasEntity_);
        }
        if (shape3D == Shape3D::SPHERE) {
            vec3 center = vec3(size_) / 2.0f;
            float radius = min(size_.x, min(size_.y, size_.z)) / 2.0f;
            for (int x = 0; x < size_.x; x++) {
                for (int y = 0; y < size_.y; y++) {
                    for (int z = 0; z < size_.z; z++) {
                        vec3 pos = vec3(x, y, z);
                        float distance = length(pos - center);
                        int index = index3DtoIndex1D(ivec3(x, y, z), size_);
                        if (distance >= radius) {
                            voxels_[index].deactivate();
                        } else {
                            voxels_[index].activate();
                        }
                        mirrorToRotationSource(index);
                    }
                }
            }
            // Sphere splits the span into active interior + inactive
            // exterior — resync from per-voxel alpha rather than picking
            // bulk active/inactive.
            IRPrefab::VoxelPool::resyncRangeFromColors(voxelStartIdx_, numVoxels_, canvasEntity_);
        }
        IRPrefab::Voxel::recomputeFaceOccupancy(voxels_, size_);
    }

    // ---- Encapsulated raw-edit API (#2165) ------------------------------
    // Supported entry points for custom carves/edits the bulk mutators above
    // don't cover. Each restores every derived invariant this set maintains
    // (rotation-source mirror -> pool active-mask -> face occupancy) once at
    // the end, so callers must NOT hand-roll syncActiveMask() /
    // recomputeFaceOccupancy() — dropping the recompute renders a carved set
    // black under the lit/rotated path (the #2018/#2117/#2146 footgun).

    // Apply `fn(index, voxel, localPos)` to every voxel, then resync once.
    // `localPos` is the voxel's local coordinate (`positions_[i].pos_`), so
    // SDF / surface / bone carves can classify by position.
    template <typename Fn> void editVoxels(Fn &&fn) {
        for (int i = 0; i < numVoxels_; ++i) {
            fn(i, voxels_[i], positions_[i].pos_);
        }
        resyncDerivedState();
    }

    // Sugar over editVoxels for the common "deactivate voxels failing a
    // predicate" carve. `shouldDeactivate(localPos)` returns true to clear.
    template <typename Fn> void carve(Fn &&shouldDeactivate) {
        editVoxels([&](int, C_Voxel &voxel, vec3 localPos) {
            if (shouldDeactivate(localPos)) {
                voxel.deactivate();
            }
        });
    }

    // Escape hatch for a multi-pass edit that writes the raw `voxels_` span
    // across several loops: do all the raw writes, then call this once.
    void resyncAfterRawEdits() {
        resyncDerivedState();
    }

    // TODO each individual voxel should be treated like this
    // and a set should only contain local positions...
    //
    // Reads positions and offsets from the live pool vectors (by index) so
    // that between-frame canvas archetype migrations — which deep-copy
    // C_VoxelPool and free the old storage — cannot produce dangling reads.
    // All four span members captured at allocation time (`positions_`,
    // `positionOffsets_`, `globalPositions_`, `voxels_`) become invalid
    // after such a migration; only `voxelStartIdx_` and `numVoxels_` remain
    // stable because they are plain scalars on this struct.
    // Returns the number of positions actually written (safeCount), or 0 if
    // the pool-bounds guard fires (numVoxels_ exceeds available pool slots).
    // The caller gates visibility — call only when the set is within the
    // shadow-feeder cull viewport (see system_update_voxel_set_children.hpp).
    // Callers use the return value to queue exactly the written range for
    // GPU upload; avoids queuing stale tail slots on bounds-guard overflow.
    int updateAsChild(
        vec3 parentPosition,
        std::vector<IRRender::VoxelGpuPosition> &poolGlobalsOut,
        const std::vector<IRRender::VoxelGpuPosition> &poolPositions,
        const std::vector<vec3> &poolOffsets
    ) {
        const size_t writableTail =
            poolGlobalsOut.size() > voxelStartIdx_ ? poolGlobalsOut.size() - voxelStartIdx_ : 0u;
        const size_t availPositions =
            poolPositions.size() > voxelStartIdx_ ? poolPositions.size() - voxelStartIdx_ : 0u;
        const size_t availOffsets =
            poolOffsets.size() > voxelStartIdx_ ? poolOffsets.size() - voxelStartIdx_ : 0u;
        const int safeCount = IRMath::min(
            numVoxels_,
            static_cast<int>(IRMath::min(IRMath::min(availPositions, availOffsets), writableTail))
        );
        for (int i = 0; i < safeCount; i++) {
            poolGlobalsOut[voxelStartIdx_ + i].pos_ = poolPositions[voxelStartIdx_ + i].pos_ +
                                                      poolOffsets[voxelStartIdx_ + i] +
                                                      parentPosition;
        }
        return safeCount;
    }

    // TODO: get rid of all unneeded voxels
    void freeInvisableVoxels(bool withAnimation = false) {
        // Voxel pool will have to resort allocation and free
        // a whole chunk at a time
    }

    // Re-derive the pool's per-slot active mask from this set's color
    // alphas. Required after any caller mutates voxel alpha through the
    // raw `voxels_` span (`voxels_[i].activate()`, `voxels_[i].deactivate()`,
    // or `voxels_[i].color_ = ...` with a different alpha) without going
    // through one of the mutator methods above. Bypassing the mutators is
    // the common pattern for SDF-carved voxel shapes that allocate a
    // dense box then deactivate exterior slots in a loop (see
    // `creations/demos/shape_debug/main.cpp::createVoxelPoolShape`).
    // Without this sync, the visibility-compact shader would emit
    // deactivated slots (mask still set from the ctor) and stage 2
    // would overwrite surface pixels with the transparent inactive
    // ones at the same iso depth.
    //
    // Prefer `editVoxels` / `carve` for new custom edits — they run this AND
    // the face-occupancy recompute for you. This stays public as the
    // low-level pool primitive (and for the pre-existing raw-loop sites).
    void syncActiveMask() {
        if (numVoxels_ <= 0) {
            return;
        }
        IRPrefab::VoxelPool::resyncRangeFromColors(
            voxelStartIdx_,
            static_cast<std::size_t>(numVoxels_),
            canvasEntity_
        );
    }

    // W-10 canvas-attach / post-load seed pass (#2217, epic #667). Moves a
    // *staged* set (headless-constructed, or freshly deserialized by
    // SaveSerialize<C_VoxelSetNew>) into a live pool span so it renders.
    // No-op unless the set is staged (`pendingVoxels_` non-empty and
    // `numVoxels_ == 0`) — that honest state, not a dirty flag, gates the
    // one-shot: once seeded the set no longer matches, so a per-frame driver
    // that calls this is self-terminating (see cpp-ecs.md "No dirty flags").
    // Target canvas resolves in priority order: explicit @p canvas > the set's
    // saved `canvasEntity_` (an id-stable C_Persistent canvas survives
    // `resetGameplay`) > the active canvas. Queues the seeded range for GPU
    // position upload; colors + active-mask ride the unconditional per-frame
    // `subData`, and lighting/AO/shadow/fog textures re-derive from the
    // re-seeded pool on the next render tick. Stays staged (returns without
    // seeding) if no live pool can be resolved.
    void attachToCanvas(IREntity::EntityId canvas = IREntity::kNullEntity) {
        if (numVoxels_ > 0 || pendingVoxels_.empty()) {
            return;
        }
        IREntity::EntityId target = canvas != IREntity::kNullEntity ? canvas : canvasEntity_;
        if (!IRPrefab::VoxelPool::hasPool(target)) {
            target = IRPrefab::VoxelPool::activeCanvasEntityOrNull();
        }
        if (!IRPrefab::VoxelPool::hasPool(target)) {
            return; // no live pool to seed into — leave the set staged
        }
        // Move the staged records out and seed from the local copy, clearing
        // pendingVoxels_ up front so the "is this set staged" gate stays honest
        // (a moved-from vector's state is unspecified). Do NOT try to keep an
        // exhausted seed retryable by leaving pendingVoxels_ in place: on a
        // pool-allocation mismatch seedIntoPool zeroes *both* numVoxels_ and
        // size_, so a later-frame retry would read extent == (0,0,0), seed zero
        // voxels into a correctly-sized allocation, and leave the set resident
        // over uninitialized slots — a silent render of garbage. A mismatch here
        // means genuine pool exhaustion (already IRE_LOG_ERROR'd), and a world
        // load frees no pool space between frames, so the retry never recovers
        // anyway — it only churns SEED_STAGED_VOXELS every frame. Drop the set
        // to a clean non-resident no-render instead; a clean no-render beats a
        // corrupt render.
        std::vector<C_Voxel> staged = std::move(pendingVoxels_);
        pendingVoxels_.clear();
        seedIntoPool(pendingBoundsMin_, staged, target);
        if (numVoxels_ > 0) {
            IRPrefab::VoxelPool::queuePositionRange(
                voxelStartIdx_,
                static_cast<std::size_t>(numVoxels_),
                canvasEntity_
            );
        }
    }

    // int addVoxelSceneNode

  private:
    // Allocate a pool span on @p canvas and seed it from the dense box @p src
    // (row-major over `size_`), placing each local voxel position at
    // `boundsMin + index`. Captures the four pool spans, resyncs the pool
    // active-mask from per-voxel alpha, and recomputes face occupancy — leaving
    // the set pool-resident (`numVoxels_ > 0`), or empty (`numVoxels_ == 0`) on
    // an allocation mismatch. `size_` must already be set and
    // `src.size() == product(size_)`. Shared by the dense-data ctor and the
    // post-load `attachToCanvas` seed pass (#2217, W-10) so the allocate +
    // seed + resync sequence lives in exactly one place.
    void seedIntoPool(ivec3 boundsMin, std::span<const C_Voxel> src, IREntity::EntityId canvas) {
        canvasEntity_ = canvas;
        const ivec3 extent = size_;
        const std::size_t requestedVoxels = src.size();
        auto allocation = IRPrefab::VoxelPool::allocate(
            static_cast<unsigned int>(requestedVoxels),
            canvasEntity_
        );
        voxelStartIdx_ = allocation.startIndex_;
        positions_ = allocation.positions_;
        positionOffsets_ = allocation.positionOffsets_;
        globalPositions_ = allocation.positionGlobals_;
        voxels_ = allocation.voxels_;

        numVoxels_ = static_cast<int>(IRMath::min(
            IRMath::min(positions_.size(), positionOffsets_.size()),
            IRMath::min(globalPositions_.size(), voxels_.size())
        ));
        if (static_cast<std::size_t>(numVoxels_) != requestedVoxels) {
            IRE_LOG_ERROR(
                "VoxelSet seed allocation mismatch: requested={}, positions={}, voxels={}",
                requestedVoxels,
                positions_.size(),
                voxels_.size()
            );
            // Release whatever the allocator handed back — `numVoxels_` is the
            // min-span count, which on today's allocator either equals
            // `requestedVoxels` (no mismatch, branch not taken) or is 0
            // (out-of-voxels assert fall-through, no slots reserved — a no-op).
            // Zeroing `numVoxels_` keeps `onDestroy()`'s guard correct.
            IRPrefab::VoxelPool::deallocate(
                voxelStartIdx_,
                static_cast<size_t>(numVoxels_),
                canvasEntity_
            );
            size_ = ivec3(0);
            numVoxels_ = 0;
            return;
        }

        const vec3 originOffset{boundsMin};
        for (int x = 0; x < extent.x; ++x) {
            for (int y = 0; y < extent.y; ++y) {
                for (int z = 0; z < extent.z; ++z) {
                    const int idx = index3DtoIndex1D(ivec3(x, y, z), extent);
                    positions_[idx] =
                        IRRender::VoxelGpuPosition{vec3(x, y, z) + originOffset, 0.0f};
                    voxels_[idx] = src[idx];
                }
            }
        }
        // Dense payload is a mix of active and inactive slots, so resync from
        // per-voxel alpha rather than the fast bulk path.
        IRPrefab::VoxelPool::resyncRangeFromColors(voxelStartIdx_, numVoxels_, canvasEntity_);
        IRPrefab::Voxel::recomputeFaceOccupancy(voxels_, extent);
    }

    // Single home for the resync order the bulk mutators run inline after a
    // raw `voxels_` edit: per-voxel rotation-source mirror -> pool
    // active-mask -> face occupancy. All three encapsulated entry points
    // (`editVoxels`, `carve`, `resyncAfterRawEdits`) route through here so the
    // invariant ordering lives in exactly one spot.
    void resyncDerivedState() {
        for (int i = 0; i < numVoxels_; ++i) {
            mirrorToRotationSource(i);
        }
        IRPrefab::VoxelPool::resyncRangeFromColors(voxelStartIdx_, numVoxels_, canvasEntity_);
        IRPrefab::Voxel::recomputeFaceOccupancy(voxels_, size_);
    }
};

} // namespace IRComponents

#endif /* COMPONENT_VOXEL_SET_H */
