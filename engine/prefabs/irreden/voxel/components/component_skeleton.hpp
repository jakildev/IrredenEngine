#ifndef COMPONENT_SKELETON_H
#define COMPONENT_SKELETON_H

// C_Skeleton — rig-root component listing the rig's joint entities in
// canonical GPU-upload order. Each entry in `joints_` is an EntityId of a
// joint entity (carrying C_Joint + the engine's canonical local-transform
// component) related to the rig root via CHILD_OF.
//
// The position of a joint in `joints_` IS the bone_id used by C_Voxel.bone_id_
// and indexed by the per-frame GPU joint-matrix SSBO (binding
// `kBufferIndex_JointTransforms` in `ir_render_types.hpp`). The index
// space is stable across saves and severance — see "Severance leaves holes"
// below — so re-baking voxel bone_ids is not required when a joint detaches.
//
// Replaces the SoA C_JointHierarchy. The legacy component remains for one
// release as a deprecation shim; new rigs use C_Skeleton + per-joint entities.
//
// ## Joint entity shape
//
// Each entity listed in `joints_` carries:
//   - C_Joint (tag — drives archetype queries like `<C_Joint, C_LocalTransform>`).
//   - The engine's canonical local-transform component (C_LocalTransform,
//     since #731 Phase 1 landed — PR #749). C_Skeleton intentionally does NOT
//     name a transform component in its API — joints carry whatever the
//     engine's canonical transform is at spawn time, and
//     SYSTEM_PROPAGATE_TRANSFORM composes the parent chain uniformly with
//     every other CHILD_OF hierarchy.
//   - (Optional) C_JointName for editor / animation lookup by bone name.
//   - (Optional) gameplay components — IK targets, constraints, hit-boxes,
//     sound emitters, particle attachments. The whole point of the
//     entity-based model is that any ECS component composes onto a joint.
//
// ## Severance leaves holes, not shifts
//
// `IRPrefab::Skeleton::severJoint(rigRoot, joint)` (future ticket — declared
// on the design side, not implemented here) removes the CHILD_OF relation
// between `joint` and `rigRoot`, walks descendants (also C_Joint, also
// orphaned), and bakes the world position of each voxel skinned to a
// severed bone into a new free-flying C_VoxelSetNew. The slot in
// `joints_` is left as kNullEntity rather than spliced out — this keeps the
// bone-index space stable so voxel `bone_id`s remain valid without a
// re-bake pass. The matching slot in the GPU SSBO is uploaded as identity
// so any pre-severance voxels still referencing the slot render in their
// last bound world transform until the asset is reloaded.
//
// ## Bind pose
//
// Skinning math needs the bind-pose inverse to recover skinning matrices.
// `bindPose_` (added in #605 Phase 2 / #1602) holds joint `i`'s rest transform
// in rig-root-local space — the same space `C_WorldTransform` reports for a
// joint left at rest, so `IRPrefab::Skeleton::skinMatrix(jointWorld, bindPose_[i])`
// returns identity at the bind pose and the joint's posed motion otherwise.
// Populate it from a `.rig` via `IRPrefab::Rig::bindPose(rig)`, which composes
// the JNTS rest chain — NOT the `.rig` BIND chunk, which stores named
// attachment points (`C_BindPoints`) unrelated to per-joint skinning despite
// the chunk name. The slot order matches `joints_`, so a kNullEntity severance
// hole keeps its (now-unused) bind slot.

#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/ir_math.hpp>

#include <vector>

namespace IRComponents {

struct C_Skeleton {
    std::vector<IREntity::EntityId> joints_;
    std::vector<IRMath::SQT> bindPose_;

    C_Skeleton() = default;
};

} // namespace IRComponents

#endif /* COMPONENT_SKELETON_H */
