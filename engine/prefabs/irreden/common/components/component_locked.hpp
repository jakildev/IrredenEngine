#ifndef COMPONENT_LOCKED_H
#define COMPONENT_LOCKED_H

namespace IRComponents {

// Tag marking an entity as excluded from bulk query-driven mutation commands
// (#17). The first consumer is `Command<RANDOMIZE_VOXELS>`, which runs
// `IRSystem::executeQuery<C_VoxelSetNew, Exclude<C_Locked>>(...)` so a
// locked voxel set keeps its colors when the rest of the scene randomizes.
// Empty tag: holds no data, only group membership (the archetype matcher's
// `Exclude<>` filter rejects nodes carrying it). Mirrors `C_Persistent`.
struct C_Locked {};

} // namespace IRComponents

#endif /* COMPONENT_LOCKED_H */
