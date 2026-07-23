## Plan: engine/system: populate the dormant SystemName registry; retire prefab wire-once system handles

- **Issue:** #2526
- **Model:** opus
- **Date:** 2026-07-22

### Scope

Make `SystemManager` the owner of the `SystemName -> SystemId` mapping by populating its existing dormant `m_engineSystemIds` member, and rewrite the two prefab wire-once handles over it. No behavior change for creations beyond deleting the now-unneeded wire-once calls.

### Approach

1. In the enum-templated registration path (`IRSystem::createSystem<SystemName type>` and `registerSystem<N, ...>` — both have the enum statically and funnel into the manager), record `m_engineSystemIds[N] = id`. `IR_ASSERT` on duplicate registration of the same `SystemName` (verified at plan time: no in-tree creation registers a `SystemName` twice; scene transitions re-register *pipelines*, not systems). Dynamic systems (`createSystemDynamic`, Lua-defined) have no enum and are out of scope.
2. Expose `SystemManager::findEngineSystem(SystemName) const` returning `SystemId` or `kNullEntity`, plus an `IRSystem::findSystem(SystemName)` free-function wrapper in `ir_system.hpp`.
3. Rewrite `IRPrefab::JointTransform::system()` and `IRPrefab::VoxelTransform::allocator()` to resolve via `findSystem(UPDATE_JOINT_MATRICES)` / `findSystem(UPDATE_VOXEL_POSITIONS_GPU)`; delete `g_jointMatrixSystem` / `g_allocatorSystem`.
4. Keep `setSystem(SystemId)` / `setAllocatorSystem(SystemId)` as deprecated no-ops: `// DEPRECATED — registration self-wires via SystemManager; remove once out-of-tree creations migrate.` Add the matching `## Deprecated` entry to `engine/prefabs/irreden/render/CLAUDE.md`.
5. Delete the six in-tree wire-once calls: `creations/demos/shape_debug/main.cpp`, `creations/demos/skeletal_demo/main.cpp`, `creations/editors/voxel_editor/main.cpp` (two each).

### Affected files

- `engine/system/include/irreden/system/system_manager.hpp` (populate map + `findEngineSystem`)
- `engine/system/include/irreden/ir_system.hpp` (`findSystem` free function)
- `engine/prefabs/irreden/render/systems/system_update_joint_matrices.hpp`
- `engine/prefabs/irreden/render/systems/system_update_voxel_positions_gpu.hpp`
- `engine/prefabs/irreden/render/CLAUDE.md` (`## Deprecated`)
- `creations/demos/shape_debug/main.cpp`, `creations/demos/skeletal_demo/main.cpp`, `creations/editors/voxel_editor/main.cpp`

### Acceptance criteria

As in the issue body. Verification: `fleet-build --target IRShapeDebug` + `fleet-run IRShapeDebug --auto-screenshot`; same for `IRSkeletalDemo`; grep for mutable `inline` namespace-scope variables under `engine/prefabs/**/systems/`.

### Gotchas

- `findSystem` costs one hash lookup; `JointTransform::slotBase` / `VoxelTransform::acquireSlot` are per-rig-operation and per-set-allocation calls, not per-voxel — no hot-path concern. If a caller ever appears in a per-entity tick, cache the resolved pointer in that system's `beginTick`, per the ECS footgun rule.
- The registry must NOT be cleared by `clearPipeline` — systems are never destroyed and pipelines re-register across scene transitions. It dies with the manager at World teardown, which is exactly the ownership the current globals lack.
- `SystemId` is an entity-id alias; `kNullEntity` is the established "unwired" sentinel both handles already use — keep it as `findSystem`'s miss value so `system()` / `allocator()` null-return semantics are unchanged.
