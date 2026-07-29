## Plan: engine/system: SystemId 0 collides with the kNullEntity 'no such system' sentinel

- **Issue:** #2540
- **Model:** opus — bounded and fully enumerated below, but it edits ECS system-core types, changes the public `ir_system.hpp` miss-value contract, and likely stacks on an open PR; not sonnet-mechanical.
- **Date:** 2026-07-25

### Verified current state

All claims below re-verified this session by reading the code (file:line cited against the tree named in each item).

- `SystemId` is `EntityId` (`std::uint64_t`) — `engine/system/include/irreden/system/ir_system_types.hpp:10`; `kNullEntity = 0` — `engine/entity/include/irreden/entity/ir_entity_types.hpp:34`; `m_nextSystemId = 0` handed out with `m_nextSystemId++` — `system_manager.hpp:325`, `system_manager.cpp:58`. So the first system registered in a process gets id `0 == kNullEntity`, exactly as the issue states.
- **The entire fix surface lives on PR #2529** (`claude/2526-systemname-registry`, approved, unmerged at plan time). `findSystem` / `findEngineSystem` / `recordEngineSystemId` and both prefab-handle resolvers do **not** exist on master (`git grep findSystem origin/master` is empty). `findEngineSystem`'s own doc comment on that branch already cites #2540 as the fix for this caveat.
- **`SystemId` is a dense index** into parallel vectors: `m_systemNames[id]`, `m_ticks[system]`, `m_timingAccum[id]`, plus the access/cadence/params vectors kept in lockstep by `createSystem`/`createSystemDynamic` (`system_manager.hpp:296,179,302`; the ctor range-for resets `m_timingAccum` wholesale). `getSystemCount()` returns `m_nextSystemId` and there is a range assert `system < m_nextSystemId` (`system_manager.cpp:221`). This is why option 1 (reserve id 0) is rejected — see Approach.
- **Exhaustive sentinel-compare audit** (branch `claude/2526-systemname-registry`, whole tree greps on `findSystem`, `SystemId.*kNullEntity`, and `SystemId x = 0` forms). Sites where a `SystemId` participates in a `kNullEntity`/zero sentinel convention:
  1. `engine/system/include/irreden/system/system_manager.hpp:342` — `findEngineSystem` miss return.
  2. `engine/system/include/irreden/ir_system.hpp:468` — `findSystem` null-manager return.
  3. `engine/prefabs/irreden/render/systems/system_update_voxel_positions_gpu.hpp:311` — `IRPrefab::VoxelTransform::allocator()` compare.
  4. `engine/prefabs/irreden/render/systems/system_update_joint_matrices.hpp:420` — `IRPrefab::JointTransform::system()` compare.
  5. `engine/prefabs/irreden/render/systems/system_debug_culling_minimap.hpp:229,230,242,243` (member + params-struct defaults) and `:352,385` (compares) — "unwired" sentinel on `SystemId` members.
  6. `creations/demos/lua_widgets/main_lua.cpp:57` — `g_dispatchId = kNullEntity` default (assigned at :204, passed at :230, never compared).
  7. `creations/demos/scene_reset/main.cpp:93-97` — five `SystemId … = 0;` defaults (all unconditionally assigned at :147-152 before use, never compared).
  8. `creations/demos/lighting/common/lighting_demo_scene.hpp:163,168` — `{}`-value-initialized `SystemId` globals (assigned during scene setup at :583,:588 before the reads at :492/:623-624).
  9. `test/system/register_system_test.cpp:174,187` — `EXPECT_EQ(findSystem(...), IREntity::kNullEntity)` miss expectations.
  All other `kNullEntity` comparisons under `engine/` and `creations/` are `EntityId`-typed (modifier resolve, hover detect, canvas/camera/fog/gizmo entities — checked) and are out of scope.
- **Lua boundary**: `SystemId`s cross into Lua as `lua_Integer` (`IRSystem.registerSystem` return, `IRSystem.systemId(name)`), but the Lua-side miss contract is **raise a Lua error**, not return-a-sentinel (`engine/script/CLAUDE.md:810-813`). No Lua code compares against a numeric sentinel, so the new sentinel never needs to cross the boundary.

No phase-0 probe is needed: every premise above is a static structural fact verified by reading the source, not a runtime mechanism claim.

### Scope

Give `SystemId` its own "no such system" sentinel that no real id can ever equal, migrate every enumerated sentinel site to it, and remove the first-system-is-indistinguishable caveat from `findSystem`. One task, one PR; no split.

### Approach

**Committed approach: option 2 — a dedicated `kNullSystemId`, value `std::numeric_limits<SystemId>::max()`.**

Why not the alternatives:
- *Option 1 (start `m_nextSystemId` at 1)*: fights the id-as-dense-index invariant. Either every parallel vector needs a phantom slot 0 (corrupting `getSystemCount()` and every whole-vector range-for), or every index site needs a `-1` shift. Larger blast radius and a permanent invariant tax for zero extra benefit.
- *Option 3 (`std::optional<SystemId>` return)*: changes the call-site shape of a query API, breaks the established sentinel idiom (`kNullEntity`, `kVoxelTransformStatic`), and cannot express the "unwired" default on stored `SystemId` members (audit items 5–8) without wider churn.

`max()` as the value has a robustness bonus: an accidental deref of the sentinel (e.g. `m_ticks[kNullSystemId]`) now trips the existing `system < m_nextSystemId` range assert loudly in debug instead of silently reading slot 0.

Steps, in order:

1. **Base resolution.** If PR #2529 has merged, branch from `origin/master` normally. If it is still open, claim with `--stackable-on 2529` and branch from `origin/claude/2526-systemname-registry`'s head — every file below refers to the post-#2529 tree. If #2529 merges mid-implementation, `git rebase --onto origin/master <fork-point>` per the inherited-prefix drop.
2. **Define the sentinel** in `engine/system/include/irreden/system/ir_system_types.hpp`, directly after `using SystemId = EntityId;`:
   `constexpr SystemId kNullSystemId = std::numeric_limits<SystemId>::max();`
   (add `#include <limits>`). Doc comment: why it is not `kNullEntity` (id 0 is a valid `SystemId` — the first system registered), and that it must never cross the Lua boundary (see Gotchas).
3. **Migrate the miss returns**: `findEngineSystem` (`system_manager.hpp:342`) and `findSystem`'s null-manager arm (`ir_system.hpp:468`). Delete `findEngineSystem`'s caveat paragraph (it cites #2540 as this fix) and update both doc comments.
4. **Migrate the compares and defaults**: audit items 3–8 above — the two prefab-handle resolvers, the four minimap defaults + two compares, `g_dispatchId`, the five `scene_reset` zero-defaults, and the two `lighting_demo_scene` `{}`-inits. Items 6–8 are never compared today (proven above), but migrating the defaults makes "not yet assigned" loud (range assert) instead of silently aliasing slot 0, and leaves the tree with a single convention.
5. **Update tests** (`test/system/register_system_test.cpp`): swap the `:174`/`:187` miss expectations to `kNullSystemId`, and add the positive-fire regression test (Acceptance criteria).
6. **Update docs**: `engine/system/CLAUDE.md` §`findSystem` (`:103` miss value, `:119` caveat bullet — replace with a pointer to `kNullSystemId`). Check `engine/prefabs/irreden/render/CLAUDE.md:625-626` migration-table rows at impl time — they name the resolution path but not the sentinel value, so likely no edit. Do **not** edit `.fleet/plans/issue-2526.md` (historical record).
7. **Sweep gate**: `git grep -nE "kNullEntity" -- engine/system engine/prefabs creations test | grep -i system` must come back empty of `SystemId`-typed hits before the PR opens.

### Affected files

- `engine/system/include/irreden/system/ir_system_types.hpp` — add `kNullSystemId` + `<limits>` include + doc comment
- `engine/system/include/irreden/system/system_manager.hpp` — `findEngineSystem` miss return; drop the caveat paragraph
- `engine/system/include/irreden/ir_system.hpp` — `findSystem` null-manager return; doc comment
- `engine/prefabs/irreden/render/systems/system_update_voxel_positions_gpu.hpp` — `allocator()` compare + doc
- `engine/prefabs/irreden/render/systems/system_update_joint_matrices.hpp` — `system()` compare + doc
- `engine/prefabs/irreden/render/systems/system_debug_culling_minimap.hpp` — 4 defaults + 2 compares
- `creations/demos/lua_widgets/main_lua.cpp` — `g_dispatchId` default
- `creations/demos/scene_reset/main.cpp` — 5 zero-defaults
- `creations/demos/lighting/common/lighting_demo_scene.hpp` — 2 `{}`-init globals
- `test/system/register_system_test.cpp` — 2 expectation swaps + 1 new test
- `engine/system/CLAUDE.md` — miss value + caveat bullet

### Acceptance criteria

- **Positive-fire regression test** (new, in `register_system_test.cpp`): in a fresh fixture, create `TEST_REGISTER_SYSTEM_A` as the **first** system and never create `TEST_REGISTER_SYSTEM_B`. Assert:
  - `ASSERT_EQ(sysA, 0u)` — documents that the collision premise (first id is 0) still holds; if `SystemManager` ever pre-registers systems this flags the premise change;
  - `EXPECT_EQ(IRSystem::findSystem(TEST_REGISTER_SYSTEM_A), sysA)`;
  - `EXPECT_NE(IRSystem::findSystem(TEST_REGISTER_SYSTEM_A), IRSystem::findSystem(TEST_REGISTER_SYSTEM_B))` — **fails before this fix** (both sides are 0) and passes after: the observable "registered-first vs never-registered are distinguishable" check.
- Updated miss expectations: `FindSystemReportsANameThatWasNeverCreated` and `FindSystemNoManagerTest` expect `kNullSystemId`; the latter (plus the headless `voxel_bone_slot_seed_test` path) proves the handles' "unwired → nullptr" behavior is preserved.
- `IrredenEngineTest` green; `fleet-build --target IRShapeDebug` green (compiles the touched prefab headers + shape_debug); the touched demos (`scene_reset`, `lua_widgets`, lighting demos) still build.
- The step-7 sweep gate returns no `SystemId`-typed `kNullEntity` hits.

### Gotchas

- **Never pass `kNullSystemId` across the Lua boundary.** LuaJIT numbers are doubles; `2^64-1` is not exactly representable. The Lua miss contract is already "raise a Lua error" (`IRSystem.systemId`) — keep it that way; the sentinel stays C++-side.
- **`IR_RELEASE` strips `IR_ASSERT`**, so an unguarded `getSystemParams(kNullSystemId)` in release is an out-of-bounds vector read. Same failure class as today's silent slot-0 read, not worse — but always sentinel-check before indexing (every migrated site already does).
- **`recordEngineSystemId` double-report is legitimate** (nested `createSystem<N>` → `System<N>::create()` → `registerSystem<N,...>` both record the same id) — do not "fix" the try_emplace/assert dance while in there.
- The two current handle consumers degrade gracefully on a false miss, so nothing visual changes in any demo — this is a correctness fix with test-level evidence only; no screenshots or render-verify needed.
- If stacked on #2529 and it merges during review, strip any stale `Stacked on:` line from the PR body after the `--onto` drop.

