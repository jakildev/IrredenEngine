#ifndef COMPONENT_SKELETON_H
#define COMPONENT_SKELETON_H

// C_Skeleton — rig-root component listing the rig's joint entities in
// canonical GPU-upload order. Each entry in `joints_` is an EntityId of a
// joint entity (carrying C_Joint + the engine's canonical local-transform
// component) related to the rig root via CHILD_OF.
//
// The position of a joint in `joints_` IS the bone_id stored in
// C_Voxel.bone_id_. At skinning time, UPDATE_JOINT_MATRICES
// sets each voxel's transform slot in LocalVoxelPositions (binding
// kBufferIndex_LocalVoxelPositions, slot 17) to `slotBase + bone_id`,
// where slotBase is the skeleton's contiguous block in EntityTransformBuffer
// (binding kBufferIndex_EntityTransforms, slot 18). Binding 21
// (kBufferIndex_JointTransforms) belongs to the SDF-shapes path and is not
// used for voxel skinning. The index space is stable across saves and
// detached joints, so voxel bone_ids never need a re-bake.
//
// The SoA C_JointHierarchy is the deprecated alternative; rigs use
// C_Skeleton + per-joint entities.
//
// ## Joint entity shape
//
// Each entity listed in `joints_` carries:
//   - C_Joint (tag — drives archetype queries like `<C_Joint, C_LocalTransform>`).
//   - The engine's canonical local-transform component (C_LocalTransform).
//     C_Skeleton intentionally does NOT
//     name a transform component in its API — joints carry whatever the
//     engine's canonical transform is at spawn time, and
//     SYSTEM_PROPAGATE_TRANSFORM composes the parent chain uniformly with
//     every other CHILD_OF hierarchy.
//   - (Optional) C_JointName for editor / animation lookup by bone name.
//   - (Optional) gameplay components — IK targets, constraints, hit-boxes,
//     sound emitters, particle attachments. The whole point of the
//     entity-based model is that any ECS component composes onto a joint.
//
// ## Holes, not shifts
//
// A `kNullEntity` entry in `joints_` is a hole: the slot keeps its index
// rather than being spliced out, so the bone-index space stays stable and
// voxel `bone_id`s remain valid. UPDATE_JOINT_MATRICES identity-fills the
// hole's transform slot, so voxels still referencing it are not deformed.
//
// ## Bind pose
//
// Skinning math needs the bind-pose inverse to recover skinning matrices.
// `bindPose_` holds joint `i`'s rest transform
// in rig-root-local space — the same space `C_WorldTransform` reports for a
// joint left at rest, so `IRPrefab::Skeleton::skinMatrix(jointWorld, bindPose_[i])`
// returns identity at the bind pose and the joint's posed motion otherwise.
// Populate it from a `.rig` via `IRPrefab::Rig::bindPose(rig)`, which composes
// the JNTS rest chain — NOT the `.rig` BIND chunk, which stores named
// attachment points (`C_BindPoints`) unrelated to per-joint skinning despite
// the chunk name. The slot order matches `joints_`, so a kNullEntity hole
// keeps its unused bind slot.

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
