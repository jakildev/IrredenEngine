#ifndef COMPONENT_VOXEL_POOL_TEARDOWN_HOOK_H
#define COMPONENT_VOXEL_POOL_TEARDOWN_HOOK_H

#include <irreden/ir_entity.hpp>

namespace IRComponents {

// Per-world bookkeeping singleton (`IREntity::singleton<T>()`): the id of the
// canvas-teardown pre-destroy hook this world's `EntityManager` carries.
// Pre-destroy hooks live on the manager and die with it, so "is the hook
// registered" is world-scoped state and belongs in the ECS rather than in a
// file-scope flag that would survive into the next world and suppress the
// registration there (`.claude/rules/cpp-globals.md`).
//
// Written only by `IRPrefab::VoxelPool::ensureCanvasTeardownHook`
// (`voxel/voxel_pool_teardown.hpp`), which is what makes that function
// idempotent across the several canvases a world creates.
struct C_VoxelPoolTeardownHook {
    IREntity::PreDestroyHookId hookId_ = IREntity::kInvalidPreDestroyHookId;
};

} // namespace IRComponents

#endif /* COMPONENT_VOXEL_POOL_TEARDOWN_HOOK_H */
