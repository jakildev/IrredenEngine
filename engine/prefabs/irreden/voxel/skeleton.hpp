#ifndef IR_PREFAB_VOXEL_SKELETON_H
#define IR_PREFAB_VOXEL_SKELETON_H

// IRPrefab::Skeleton — skinning helpers over C_Skeleton + its bind pose
// (C_Skeleton.bindPose_). The transform algebra (SQT compose / inverse) lives
// in IRMath; this prefab-layer free-function surface joins a joint's live
// C_WorldTransform to its rest pose. The companion severJoint API documented
// on C_Skeleton lands with a later #605 Phase 2+ ticket.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_world_transform.hpp>

namespace IRPrefab::Skeleton {

// skinMatrix = jointWorld × bindInverse: the rigid motion that carries a voxel
// from its bind-pose position to the joint's current world pose. At the bind
// pose (jointWorld == bind) it is identity; under #605 Phase 2.3 the
// per-joint result is written into the binding-18 transform buffer and the
// existing c_update_voxel_positions prepass applies it to each skinned voxel.
// The bind SQT is inverted analytically (IRMath::sqtInverse), not via a 4×4
// inverse — see the gotcha on #1602.
inline IRMath::mat4 skinMatrix(const IRMath::SQT &jointWorld, const IRMath::SQT &bind) {
    return IRMath::sqtToMat4(jointWorld) * IRMath::sqtToMat4(IRMath::sqtInverse(bind));
}

// Convenience overload reading the joint entity's current C_WorldTransform.
// Entry-point style: a one-time read (spawn, editor interaction), NOT a
// per-frame tick. The joint-matrix uploader (#605 Phase 2.2 / #1603) iterates
// joints with C_WorldTransform in its template params instead of calling this
// per joint.
inline IRMath::mat4 skinMatrix(IREntity::EntityId joint, const IRMath::SQT &bind) {
    const auto &world = IREntity::getComponent<IRComponents::C_WorldTransform>(joint);
    return skinMatrix(IRMath::SQT{world.scale_, world.rotation_, world.translation_}, bind);
}

} // namespace IRPrefab::Skeleton

#endif /* IR_PREFAB_VOXEL_SKELETON_H */
