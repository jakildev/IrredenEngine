#ifndef IRREDEN_VOXEL_SET_CULL_GATE_H
#define IRREDEN_VOXEL_SET_CULL_GATE_H

// The visibility predicate the per-set pose writers (UPDATE_VOXEL_SET_CHILDREN
// and both REBUILD_GRID_VOXELS arms) cull with. It is the only honest skip for
// that work (`cpp-ecs.md`): a set is written when its pool chunks' stored
// bounds overlap the viewport, so a set leaving the viewport is written out of
// it, or when its current transform places it inside, so a set whose stored
// bounds sit off-view where it was last written is re-admitted on its return.

#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/voxel/grid_rotation.hpp>

#include <cstddef>

namespace IRPrefab::Voxel {

// World AABB of voxel-cell centres containing every pool global the pose
// writers produce for @p voxelSet under @p worldTransform, in O(1) from the
// authored box (local origin + `size_`), without reading the written span.
//
// Identity transform: the box translated, exactly what both writers emit.
// Otherwise a rotation-independent cube about the translation, with a radius of
// max(|scale|, 1) times the farthest corner of the authored box grown by a
// cell. That cube holds the inverse resample's destination cells (whose
// pre-images round into the box), the forward map's rounded cells, and the
// translation-only baseline UPDATE_VOXEL_SET_CHILDREN writes first.
//
// Per-voxel squash-stretch offsets are not modelled; they grow with the set's
// size, so the viewport margin does not bound them. The accepted edge: a
// deformed set returning into view is re-admitted only once its undeformed box
// reaches the viewport, possibly a frame or two after its deformed tip.
// Stored-bounds admission still covers every set that is being written.
//
// Precondition: @p voxelSet is pool-resident in @p pool (`numVoxels_ > 0`).
inline IRComponents::ChunkWorldBounds destinationBounds(
    const IRComponents::C_VoxelSetNew &voxelSet,
    const IRComponents::C_VoxelPool &pool,
    const IRComponents::C_WorldTransform &worldTransform
) {
    // Slot 0 holds the authored float origin, which stays authored while a GRID
    // spin rewrites only globals and colours; `localOriginMin()` rounds a
    // half-integer CENTER / GROUND origin away from it.
    const IRMath::vec3 origin = pool.getPositions()[voxelSet.voxelStartIdx_].pos_;
    const IRMath::vec3 boxMax = origin + IRMath::vec3(voxelSet.size_ - IRMath::ivec3(1));
    if (IRPrefab::GridRotation::isIdentityTransform(worldTransform)) {
        return {origin + worldTransform.translation_, boxMax + worldTransform.translation_};
    }
    const IRMath::vec3 reach = IRMath::max(
        IRMath::abs(origin - IRMath::vec3(1.0f)),
        IRMath::abs(boxMax + IRMath::vec3(1.0f))
    );
    const IRMath::vec3 scale = IRMath::abs(worldTransform.scale_);
    const float maxScale = IRMath::max(1.0f, IRMath::max(scale.x, IRMath::max(scale.y, scale.z)));
    // One more cell absorbs the forward map's round-half-up.
    const IRMath::vec3 radius(maxScale * IRMath::length(reach) + 1.0f);
    return {worldTransform.translation_ - radius, worldTransform.translation_ + radius};
}

// True when the pose writers must write @p voxelSet this tick against the
// cull @p viewport. The destination test runs only when the stored test
// rejects, so a visible set pays nothing extra.
inline bool isVoxelSetCullVisible(
    const IRComponents::C_VoxelSetNew &voxelSet,
    const IRComponents::C_VoxelPool &pool,
    const IRComponents::C_WorldTransform &worldTransform,
    const IRMath::IsoBounds2D &viewport
) {
    if (pool.isRangeVisible(
            voxelSet.voxelStartIdx_,
            static_cast<std::size_t>(voxelSet.allocatedVoxels_),
            viewport
        )) {
        return true;
    }
    if (voxelSet.voxelStartIdx_ >= pool.getPositions().size()) {
        return true;
    }
    const IRComponents::ChunkWorldBounds bounds = destinationBounds(voxelSet, pool, worldTransform);
    return pool.isWorldBoundsVisible(bounds.worldMin_, bounds.worldMax_, viewport);
}

} // namespace IRPrefab::Voxel

#endif /* IRREDEN_VOXEL_SET_CULL_GATE_H */
