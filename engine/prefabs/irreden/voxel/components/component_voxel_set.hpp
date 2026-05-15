#ifndef COMPONENT_VOXEL_SET_H
#define COMPONENT_VOXEL_SET_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/texture.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/systems/system_voxel_pool.hpp>

#include <vector>

using namespace IRMath;
using IRRender::ImageData;
using IRRender::ResourceId;
using IRRender::Texture2D;
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

    // Index into the canvas voxel pool's underlying arrays. Captured at
    // allocation time so consumers never recompute it from a pointer-diff
    // against a separately-cached @c C_VoxelPool* (a stale cached pool
    // pointer made the diff resolve to a wild index).
    size_t voxelStartIdx_ = 0;

    // local voxel position
    std::span<C_Position3D> positions_;

    // Per-voxel deformation offset authored by VOXEL_SQUASH_STRETCH.
    // Internal pool scratch state — not the engine-level entity offset
    // channel (which travels through the modifier framework's
    // POSITION_OFFSET_3D vec3 field and lands on C_PositionGlobal3D).
    std::span<vec3> positionOffsets_;

    // global voxel position recalculated each update
    std::span<C_PositionGlobal3D> globalPositions_;

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

    C_VoxelSetNew(
        ivec3 size, Color color = IRColors::kGreen, bool centerAroundOrigin = false
        // int voxelPoolId = 0
    )
        : numVoxels_{size.x * size.y * size.z}
        , size_{size} {
        const int requestedVoxels = size.x * size.y * size.z;
        canvasEntity_ = IRRender::getActiveCanvasEntity();
        auto allocation = IRRender::allocateVoxels(requestedVoxels);
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
            IRRender::deallocateVoxels(voxelStartIdx_, static_cast<size_t>(numVoxels_));
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
                    positions_[index3DtoIndex1D(ivec3(x, y, z), size)] = C_Position3D{pos};
                    voxels_[index3DtoIndex1D(ivec3(x, y, z), size)].color_ = color;
                }
            }
        }
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
    C_VoxelSetNew(ivec3 boundsMin, ivec3 boundsMax, std::span<const C_Voxel> voxels)
        : numVoxels_{0}
        , size_{boundsMax - boundsMin} {
        canvasEntity_ = IRRender::getActiveCanvasEntityOrNull();
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

        auto allocation = IRRender::allocateVoxels(static_cast<unsigned int>(requestedVoxels));
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
            IRRender::deallocateVoxels(voxelStartIdx_, static_cast<size_t>(numVoxels_));
            size_ = ivec3(0);
            numVoxels_ = 0;
            return;
        }

        const vec3 originOffset{boundsMin};
        for (int x = 0; x < extent.x; ++x) {
            for (int y = 0; y < extent.y; ++y) {
                for (int z = 0; z < extent.z; ++z) {
                    const int idx = index3DtoIndex1D(ivec3(x, y, z), extent);
                    positions_[idx] = C_Position3D{vec3(x, y, z) + originOffset};
                    voxels_[idx] = voxels[idx];
                }
            }
        }
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
            IRRender::deallocateVoxels(voxelStartIdx_, static_cast<size_t>(numVoxels_));
            IRE_LOG_DEBUG("Deallocated {} voxels", numVoxels_);
        }
    }

    void changeVoxelColor(ivec3 index, Color color) {
        voxels_[index3DtoIndex1D(index, size_)].color_ = color;
    }

    void changeVoxelColorAll(Color color) {
        for (int i = 0; i < numVoxels_; i++) {
            voxels_[i].color_ = color;
        }
    }

    void deactivateAll() {
        for (int i = 0; i < numVoxels_; i++) {
            voxels_[i].deactivate();
        }
    }

    void activateAll() {
        for (int i = 0; i < numVoxels_; i++) {
            voxels_[i].activate();
        }
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
        }
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
    bool updateAsChild(
        C_Position3D parentPosition,
        std::vector<C_PositionGlobal3D> &poolGlobalsOut,
        const std::vector<C_Position3D> &poolPositions,
        const std::vector<vec3> &poolOffsets
    ) {
        if (hasLastParentPosition_ && parentPosition.pos_ == lastParentPosition_) {
            return false;
        }
        lastParentPosition_ = parentPosition.pos_;
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
                                                      parentPosition.pos_;
        }
        return safeCount > 0;
    }

    // TODO: get rid of all unneeded voxels
    void freeInvisableVoxels(bool withAnimation = false) {
        // Voxel pool will have to resort allocation and free
        // a whole chunk at a time
    }

    // int addVoxelSceneNode
};

} // namespace IRComponents

#endif /* COMPONENT_VOXEL_SET_H */
