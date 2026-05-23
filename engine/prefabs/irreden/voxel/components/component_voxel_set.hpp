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
    vec3 lastParentPosition_ = vec3(0.0f);
    bool hasLastParentPosition_ = false;

    // SYSTEM_REBUILD_GRID_VOXELS push-at-mutation cache. The rebuild system
    // compares the entity's live `C_WorldTransform` against these values
    // each tick and only re-rasterizes the pool span when something
    // changed — quaternion rotation, scale, or world translation. Mirrors
    // the `lastParentPosition_` early-out UPDATE_VOXEL_SET_CHILDREN uses
    // (both systems now read `C_WorldTransform.translation_` — T-301a).
    vec4 lastRebuildWorldRotation_ = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    vec3 lastRebuildWorldScale_ = vec3(1.0f, 1.0f, 1.0f);
    vec3 lastRebuildWorldTranslation_ = vec3(0.0f);
    bool hasLastRebuildWorldTransform_ = false;

    // Index into the canvas voxel pool's underlying arrays. Captured at
    // allocation time so consumers never recompute it from a pointer-diff
    // against a separately-cached @c C_VoxelPool* (a stale cached pool
    // pointer made the diff resolve to a wild index).
    size_t voxelStartIdx_ = 0;

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

    // default constructor
    C_VoxelSetNew()
        : C_VoxelSetNew(ivec3(0, 0, 0)) {}

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
                "VoxelSet dense allocation mismatch: requested={}, positions={}, voxels={}",
                requestedVoxels,
                positions_.size(),
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
                    voxels_[idx] = voxels[idx];
                }
            }
        }
        // Dense payload is a mix of active and inactive slots, so
        // resync from per-voxel alpha rather than the fast bulk path.
        IRPrefab::VoxelPool::resyncRangeFromColors(voxelStartIdx_, numVoxels_, canvasEntity_);
        IRPrefab::Voxel::recomputeFaceOccupancy(voxels_, extent);
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
            IRPrefab::VoxelPool::deallocate(
                voxelStartIdx_,
                static_cast<size_t>(numVoxels_),
                canvasEntity_
            );
            IRE_LOG_DEBUG("Deallocated {} voxels", numVoxels_);
        }
    }

    void changeVoxelColor(ivec3 index, Color color) {
        const int idx = index3DtoIndex1D(index, size_);
        voxels_[idx].color_ = color;
        IRPrefab::VoxelPool::markVoxelActive(voxelStartIdx_, idx, color.alpha_ != 0, canvasEntity_);
    }

    void changeVoxelColorAll(Color color) {
        for (int i = 0; i < numVoxels_; i++) {
            voxels_[i].color_ = color;
        }
        if (color.alpha_ != 0) {
            IRPrefab::VoxelPool::markRangeActive(voxelStartIdx_, numVoxels_, canvasEntity_);
        } else {
            IRPrefab::VoxelPool::markRangeInactive(voxelStartIdx_, numVoxels_, canvasEntity_);
        }
    }

    void deactivateAll() {
        for (int i = 0; i < numVoxels_; i++) {
            voxels_[i].deactivate();
        }
        IRPrefab::VoxelPool::markRangeInactive(voxelStartIdx_, numVoxels_, canvasEntity_);
        IRPrefab::Voxel::recomputeFaceOccupancy(voxels_, size_);
    }

    void activateAll() {
        for (int i = 0; i < numVoxels_; i++) {
            voxels_[i].activate();
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
    // the parent position is unchanged (early-out). Callers use the return
    // value to queue exactly the written range for GPU upload — avoids
    // queuing stale slots in the rare case the pool-bounds guard fires.
    int updateAsChild(
        vec3 parentPosition,
        std::vector<IRRender::VoxelGpuPosition> &poolGlobalsOut,
        const std::vector<IRRender::VoxelGpuPosition> &poolPositions,
        const std::vector<vec3> &poolOffsets
    ) {
        if (hasLastParentPosition_ && parentPosition == lastParentPosition_) {
            return 0;
        }
        lastParentPosition_ = parentPosition;
        hasLastParentPosition_ = true;
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

    // int addVoxelSceneNode
};

} // namespace IRComponents

#endif /* COMPONENT_VOXEL_SET_H */
