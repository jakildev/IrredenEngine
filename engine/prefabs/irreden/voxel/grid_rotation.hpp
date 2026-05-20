#ifndef IRREDEN_VOXEL_GRID_ROTATION_H
#define IRREDEN_VOXEL_GRID_ROTATION_H

// Grid-mode rotation math — used by SYSTEM_REBUILD_GRID_VOXELS to map
// authored local voxel positions through an entity's SQT world transform
// to integer world-grid cells.
//
// Lives outside the system header so unit tests can exercise the math
// without setting up an EntityManager + canvas + voxel pool.
//
// Semantics:
// - Identity transform (rotation = (0,0,0,1), scale = (1,1,1)): returns
//   local + parent_translation as-is (no rounding). UPDATE_VOXEL_SET_CHILDREN
//   handles this case in the regular pipeline; the helper produces the same
//   value so consumers may call uniformly without a branch.
// - Non-identity: scale, then rotate around the entity origin, then add
//   parent_translation, then round each component to the nearest integer
//   cell. Aliasing (multiple authored voxels collapsing into the same world
//   cell) is accepted by design.

#include <irreden/ir_math.hpp>
#include <irreden/common/components/component_world_transform.hpp>

namespace IRPrefab::GridRotation {

/// Returns true when the SQT carries no rotation and no scale (identity).
inline bool isIdentityTransform(const IRComponents::C_WorldTransform &wt) {
    return wt.rotation_ == IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f) &&
           wt.scale_ == IRMath::vec3(1.0f, 1.0f, 1.0f);
}

/// Computes the world-space cell for one voxel in a GRID-mode entity.
/// @p localVoxel is the authored position in entity-local space.
/// @p localOffset is the per-voxel offset authored by VOXEL_SQUASH_STRETCH.
/// Identity transforms return @c localVoxel + localOffset + translation
/// unchanged; non-identity transforms apply scale → rotate → translate and
/// snap each axis to the nearest integer cell.
inline IRMath::vec3 worldCellForGridVoxel(
    IRMath::vec3 localVoxel, IRMath::vec3 localOffset, const IRComponents::C_WorldTransform &wt
) {
    const IRMath::vec3 composed = localVoxel + localOffset;
    if (isIdentityTransform(wt)) {
        return composed + wt.translation_;
    }
    const IRMath::vec3 scaled = wt.scale_ * composed;
    const IRMath::vec3 rotated = IRMath::rotateVectorByQuat(scaled, wt.rotation_);
    const IRMath::vec3 world = wt.translation_ + rotated;
    return IRMath::vec3(IRMath::round(world.x), IRMath::round(world.y), IRMath::round(world.z));
}

} // namespace IRPrefab::GridRotation

#endif /* IRREDEN_VOXEL_GRID_ROTATION_H */
