#ifndef IRREDEN_VOXEL_GRID_ROTATION_H
#define IRREDEN_VOXEL_GRID_ROTATION_H

// Grid-mode rotation math — used by SYSTEM_REBUILD_GRID_VOXELS to map
// authored local voxel positions through an entity's SQT world transform
// to integer world-grid cells, and back (the #1720 inverse resample).
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
//
// The forward map alone is NOT surjective onto the covered world cells —
// rotating a lattice forward leaves uncovered dest cells (coverage holes,
// up to ~29% of a solid 12³ mid-rotation). SYSTEM_REBUILD_GRID_VOXELS
// therefore renders rotating sets by walking DEST cells and inverse-mapping
// each through `sourceCellForWorldCell` below (#1720, mirroring the detached
// re-voxelize fix #1619); the forward map remains the identity-path /
// creation-facing helper.
//
// Rounding uses `IRMath::roundVec3HalfUp` (floor(x + 0.5)), NOT `IRMath::round`
// (glm round-half-away-from-zero). This is the engine's CPU↔GPU coordinate
// handshake convention (`ir_math.hpp` roundHalfUp doc): the detached re-voxelize
// GPU mirror `c_revoxelize_detached.{glsl,metal}` calls the shared `roundHalfUp`
// helper, so CPU and GPU classify negative half-integers identically (#1556).
// GRID re-voxelize (`REBUILD_GRID_VOXELS`) shares this helper; the rounding
// switch only changes cells at exact negative half-integer post-rotation
// coordinates (float-measure-zero) and aligns it with the same convention the
// SDF lattice walk already uses.

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
    // roundHalfUp (not glm round) so the GPU mirror agrees byte-for-byte; see
    // the header note above.
    return IRMath::vec3(IRMath::roundVec3HalfUp(world));
}

/// Per-axis half-cell anchor of a composed local: `composed - roundHalfUp(composed)`
/// — -0.5 on axes where a center-around-origin solid authors at half-integers
/// (even-sized axes), 0 on odd. The detached re-voxelize mapping rotates
/// anchored POINTS (`cell + anchor`), not raw lattice cells; ignoring the
/// anchor shifted a rotating solid by a constant half cell per even axis
/// (#2349). This is the ONE home of the derivation — the seed
/// (`IRPrefab::DetachedRevoxelize::seedResidentLocals`) and the CPU mask twin
/// (`SYSTEM_REBUILD_DETACHED_VOXELS`) both call it; the GLSL/Metal kernels
/// receive the value via `RevoxelizeDetachedParams::anchor_`.
inline IRMath::vec3 halfCellAnchor(IRMath::vec3 composed) {
    return composed - IRMath::vec3(IRMath::roundVec3HalfUp(composed));
}

/// Anchored dest cell of a rotated detached voxel: `roundHalfUp(R·composed - anchor)`
/// — the CPU twin of `revoxSourceCellForDest`'s forward direction in
/// `c_revoxelize_detached.{glsl,metal}` (#2349), rotation about the pool
/// origin (translation 0, scale 1). Kept beside the GRID map above so the
/// CPU↔GPU roundHalfUp handshake convention stays in one header.
inline IRMath::ivec3 anchoredCellForDetachedVoxel(
    IRMath::vec3 composed, IRMath::vec4 rotation, IRMath::vec3 anchor
) {
    return IRMath::roundVec3HalfUp(IRMath::rotateVectorByQuat(composed, rotation) - anchor);
}

/// Inverse of @ref worldCellForGridVoxel's non-identity arm: maps an integer
/// world cell back to the continuous source-frame position (`local + offset`
/// space). Round the result with `IRMath::roundVec3HalfUp` to land on the
/// integer source-cell lattice the occupancy grid is keyed by — the same
/// `roundHalfUp(R⁻¹·c)` convention the detached re-voxelize GPU kernel uses
/// (#1619), so CPU GRID and GPU detached classify identically.
/// @p inverseRotation is `IRMath::quatInverse(wt.rotation_)`, hoisted by the
/// caller so a per-dest-cell loop pays one quat rotate, not an inverse too.
/// Zero scale components are the caller's responsibility to reject (a
/// zero-scaled solid has no inverse; the rebuild system skips those sets).
inline IRMath::vec3 sourceCellForWorldCell(
    IRMath::ivec3 worldCell, const IRComponents::C_WorldTransform &wt, IRMath::vec4 inverseRotation
) {
    const IRMath::vec3 rotated = IRMath::vec3(worldCell) - wt.translation_;
    const IRMath::vec3 scaled = IRMath::rotateVectorByQuat(rotated, inverseRotation);
    return scaled / wt.scale_;
}

} // namespace IRPrefab::GridRotation

#endif /* IRREDEN_VOXEL_GRID_ROTATION_H */
