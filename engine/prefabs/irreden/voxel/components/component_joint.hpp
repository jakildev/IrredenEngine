#ifndef COMPONENT_JOINT_H
#define COMPONENT_JOINT_H

// C_Joint — tag marking an entity as a skeletal joint owned by a C_Skeleton
// rig root. Pairs with the engine's canonical local-transform component on
// the same entity; CHILD_OF carries the parent reference up the bone chain.
//
// Drives archetype queries like `<C_Joint, C_LocalTransform>` so joint-only
// systems (IK solvers, GPU joint-matrix uploaders) iterate joints without
// also seeing rig roots or skinned voxel sets.
//
// See `component_skeleton.hpp` for the entity-based joint model.

namespace IRComponents {

struct C_Joint {};

} // namespace IRComponents

#endif /* COMPONENT_JOINT_H */
