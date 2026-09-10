#ifndef IR_PREFAB_VOXEL_POOL_TEARDOWN_H
#define IR_PREFAB_VOXEL_POOL_TEARDOWN_H

// Canvas-teardown contract for the per-canvas voxel pool (#2913).
//
// A `C_VoxelPool` lives on a canvas entity, and `C_VoxelSetNew` allocates
// pool-RELATIVE spans out of one specific pool — so destroying a canvas
// invalidates three things on every dependent set at once: `canvasEntity_`
// (now names a destroyed entity), `voxelStartIdx_` and `numVoxels_` (now index
// a dead pool). Nothing in the set can detect that on its own, and a bare
// `canvasEntity_` re-target would alias another pool's voxels.
//
// The sweep below re-stages those sets instead, and is wired into
// `EntityManager::destroyEntity` as a pre-destroy hook so it runs while the
// pool is still there to deallocate into. Same shape and same reason as
// `IRPrefab::Modifier::removeBySource` (`common/modifier.hpp`), which sweeps a
// dying source off live dependents before the EntityId recycles.
//
// It is a sibling header rather than part of `voxel_pool_api.hpp` because the
// sweep needs `C_VoxelSetNew`, which includes the pool API — the API header
// cannot include it back.

#include <irreden/ir_entity.hpp>

#include <irreden/voxel/components/component_voxel_pool_teardown_hook.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/voxel_pool_api.hpp>

namespace IRPrefab::VoxelPool {

/// Re-stage every `C_VoxelSetNew` whose pool lives on @p destroyed, leaving
/// each one in the staged state `C_VoxelSetNew::attachToCanvas` seeds from.
/// Call BEFORE the canvas entity is destroyed — the per-set deallocate needs
/// the pool to still exist.
///
/// Keyed on the set's `canvasEntity_`, not on the canvas's parent: a set that
/// renders into a canvas need not live on the entity that owns the wrapper.
///
/// The `hasPool` gate is load-bearing, not defensive. Registered as a
/// pre-destroy hook this runs for EVERY `destroyEntity` call, so without the
/// early-out the linear sweep would be O(sets) per entity destruction rather
/// than per canvas destruction. `hasPool` is a single `poolForCanvas` lookup,
/// which keeps non-canvas destroys O(1).
inline void restageSetsOnCanvas(IREntity::EntityId destroyed) {
    if (!hasPool(destroyed)) {
        return;
    }
    IREntity::forEachComponent<IRComponents::C_VoxelSetNew>(
        [destroyed](IRComponents::C_VoxelSetNew &set) {
            if (set.canvasEntity_ != destroyed) {
                return;
            }
            set.restageFromPool();
        }
    );
}

/// Arm `restageSetsOnCanvas` on this world's `EntityManager`, once. Called from
/// every site that attaches a `C_VoxelPool` to a canvas — `Prefab<
/// kVoxelPoolCanvas>::create` and `IRPrefab::EntityCanvas::addVoxelPool` — so
/// the teardown is wired by the act of creating the pool rather than by a
/// creation remembering to register it. `grep -rn 'C_VoxelPool{' engine` names
/// the sites that must call this.
///
/// Idempotent via the `C_VoxelPoolTeardownHook` singleton, which is world-scoped
/// like the hook vector itself: a fresh `EntityManager` starts with neither, so
/// the next world re-arms rather than inheriting a stale "already registered".
///
/// Main-thread, outside archetype iteration — the singleton's first call is a
/// `createEntity` (the caveat `render/widget_theme.hpp` documents). Both arming
/// sites already perform structural changes, so they are on a path where that
/// is legal.
inline void ensureCanvasTeardownHook() {
    IRComponents::C_VoxelPoolTeardownHook &record =
        IREntity::singleton<IRComponents::C_VoxelPoolTeardownHook>();
    if (record.hookId_ != IREntity::kInvalidPreDestroyHookId) {
        return;
    }
    record.hookId_ =
        IREntity::getEntityManager().registerPreDestroyHook([](IREntity::EntityId destroyed) {
            restageSetsOnCanvas(destroyed);
        });
}

} // namespace IRPrefab::VoxelPool

#endif /* IR_PREFAB_VOXEL_POOL_TEARDOWN_H */
