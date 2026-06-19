# Plan: test — port lua_collision_bindings_test.cpp onto the merged #1817 surface

- **Issue:** #1908
- **Model:** sonnet
- **Date:** 2026-06-19
- **Blocked by:** #1817 (PR #1905 — MERGED 2026-06-19, commit 97d5afa8)

## Scope

#1817 shipped twice in parallel. The surviving impl (PR #1905, branch
`claude/1817-lua-overlap-events`) merged the Lua overlap binding surface but
**without** a dedicated unit test. The superseded duplicate (PR #1904, branch
`claude/1817-lua-collision-events`) carried a 5-case gtest the survivor lacks.

Port that test onto the merged surface. The Lua-facing API
(`IRCollision.onOverlapEnter/onOverlapExit`) is identical between the two
branches; the deltas are entirely in the C++/enum surface the test touches.
Lift verbatim, apply the deltas in §Approach, wire into the test target, build
green.

## Source to lift from

`git show origin/claude/1817-lua-collision-events:test/script/lua_collision_bindings_test.cpp`
(230 lines). Port the **whole file**; the 4 dispatch/handler test bodies and
the test harness (`LuaCollisionBindingsTest` fixture, `makeCollider`,
`buildPipeline`, `tick`, the arcade layer constants) port **verbatim** — only
the five mechanical deltas below change.

## Approach

Create `test/script/lua_collision_bindings_test.cpp` from the #1904 source, then
apply exactly these deltas (all verified against `origin/master`):

1. **Drop the dead include.** Remove
   `#include <irreden/update/components/component_collision_pair_buffer.hpp>`.
   That component was replaced by `C_OverlapContactBatch` and **no longer
   exists on master** — leaving the include in is a hard build break. The test
   never names the buffer type directly, so no replacement include is needed
   (registering `DISPATCH_LUA_OVERLAP` pulls in
   `system_dispatch_lua_overlap.hpp`, which includes the batch component). All
   other includes in the source resolve unchanged.

2. **Rename the dispatch system enum** (lost its `COLLISION_` prefix in #1905).
   Two sites:
   - In the fixture's `registerPrefabSystems<...>()`:
     `IRSystem::COLLISION_DISPATCH_LUA_OVERLAP` → `IRSystem::DISPATCH_LUA_OVERLAP`.
   - In `buildPipeline()`'s Lua string:
     `IRSystem.SystemName.COLLISION_DISPATCH_LUA_OVERLAP`
     → `IRSystem.SystemName.DISPATCH_LUA_OVERLAP`.
   The include path `<irreden/update/systems/system_dispatch_lua_overlap.hpp>`
   is **unchanged** (only the enumerator name dropped the prefix, not the file).
   `COLLISION_NOTE_PLATFORM` and `COLLISION_EVENT_CLEAR` keep their names.

3. **Fix the Layer-table keys** in `LayerTableMirrorsCppEnum`. The merged
   binding (`lua_collision_bindings.hpp`) exposes the table with the **full
   enum-name** keys via `layer[#name]`, not short aliases. So:
   - `IRCollision.Layer.DEFAULT`      → `IRCollision.Layer.COLLISION_LAYER_DEFAULT`
   - `IRCollision.Layer.NOTE_PLATFORM`→ `IRCollision.Layer.COLLISION_LAYER_NOTE_PLATFORM`
   - `IRCollision.Layer.PARTICLE`     → `IRCollision.Layer.COLLISION_LAYER_PARTICLE`
   The C++ comparison RHS (`IRComponents::COLLISION_LAYER_DEFAULT`, etc.) stays
   as-is — `CollisionLayerMask` is an **unscoped** `enum`, so the bare
   `IRComponents::COLLISION_LAYER_*` enumerators compile. (Short keys would
   silently read `nil` → cast to `0` → spurious mismatch, so this delta is
   load-bearing.)

4. **Reorder the UPDATE pipeline** in `buildPipeline()` to mirror the merged
   demo (`creations/demos/collision_overlap_demo/main.lua`): the canonical
   order is
   **`COLLISION_EVENT_CLEAR` → `COLLISION_NOTE_PLATFORM` → `DISPATCH_LUA_OVERLAP`**.
   The #1904 source had `NOTE_PLATFORM → DISPATCH → EVENT_CLEAR`. The overlap
   ENTER/EXIT assertions are order-insensitive (DISPATCH derives enter/exit
   from the `C_OverlapContactBatch` it self-clears, independent of the
   per-entity `C_ContactEvent` that `EVENT_CLEAR` resets), but mirroring the
   demo's clear-first order is the safe, canonical choice and avoids any
   `C_ContactEvent` wasTouching_ rollover subtlety. Keep all three systems in
   both `registerPrefabSystems<>` and the pipeline.

5. **Wire into the test target.** Add `script/lua_collision_bindings_test.cpp`
   to the `add_executable(IrredenEngineTest …)` source list in
   `test/CMakeLists.txt`, alphabetically **before** `script/lua_command_test.cpp`
   (the `script/lua_*` block is sorted; `collision` < `command`).

Then `fleet-build --target IrredenEngineTest` and run the 5 new cases.

## Why the merged surface still satisfies all 5 cases (verified)

- **Pipeline producer/consumer.** `COLLISION_NOTE_PLATFORM` appends current
  overlaps to the `C_OverlapContactBatch` singleton every frame **only when the
  singleton exists**; `DISPATCH_LUA_OVERLAP::create()` creates that singleton,
  so registering the dispatcher is the opt-in. `DISPATCH_LUA_OVERLAP::tick`
  derives ENTER/EXIT at pair granularity from previous-vs-current and then
  clears the batch itself.
- **Static-transform harness.** `makeCollider` builds `C_WorldTransform`
  directly (no `C_LocalTransform`/`PROPAGATE_TRANSFORM`/`VELOCITY_3D`), and
  `NOTE_PLATFORM` scans the
  `<C_WorldTransform, C_ColliderIso3DAABB, C_CollisionLayer, C_ContactEvent>`
  archetype off the world transform — so the `EnterFiresOnce…` case can move an
  entity by writing `.translation_` then `tick()`. No velocity/propagate
  systems needed.
- **Arg order.** `DISPATCH_LUA_OVERLAP` passes the entity on the handler's
  registered `layer1` first (D4), which is what
  `ProjectileEnemyAndPickupPlayerOverlapsFireFromLua` asserts.
- **Unregistered-dispatch error.** `bindLuaDrivenEcs()` calls
  `bindCollisionEvents` (lua_script.cpp:653), so `IRCollision` is bound even
  with no prefab system registered; `resolveOverlapDispatch` then throws a
  `sol::error` (SOL_ALL_SAFETIES_ON), so `safe_script(...,
  script_pass_on_error)` returns an invalid result — exactly what
  `OnOverlapEnterRaisesWithoutDispatchSystem` checks. No change to that case.
- **Component constructors match the source verbatim:**
  `C_CollisionLayer(layer, collidesWithMask, isSolid=true)`,
  `C_ColliderIso3DAABB(vec3 halfExtents, …)`,
  `C_WorldTransform(translation, rotation, scale)` with field `.translation_`.

## Affected files

- `test/script/lua_collision_bindings_test.cpp` — **NEW**; ported from
  `origin/claude/1817-lua-collision-events` with the five deltas above.
- `test/CMakeLists.txt` — add the new source to `add_executable(IrredenEngineTest …)`.

## Acceptance criteria

- The 5 cases build and pass under `fleet-build --target IrredenEngineTest`:
  - `LuaCollisionBindingsTest.LayerTableMirrorsCppEnum`
  - `LuaCollisionBindingsTest.ProjectileEnemyAndPickupPlayerOverlapsFireFromLua`
  - `LuaCollisionBindingsTest.EnterFiresOnceExitFiresWhenOverlapEnds`
  - `LuaCollisionBindingsTest.UnmatchedLayerPairDoesNotFire`
  - `LuaCollisionBindingsUnregisteredTest.OnOverlapEnterRaisesWithoutDispatchSystem`
- No regression in the existing `IrredenEngineTest` suite.

## Gotchas

- `component_collision_pair_buffer.hpp` is gone — keeping that include is a hard
  build break (delta 1).
- `IRCollision.Layer.*` keys are the **full** `COLLISION_LAYER_*` names, not
  short aliases — short keys read `nil` and the enum-mirror test fails silently
  (delta 3).
- The system enum is `DISPATCH_LUA_OVERLAP`, not `COLLISION_DISPATCH_LUA_OVERLAP`
  (delta 2) — but the header filename did NOT change.
- `EVENT_CLEAR` does NOT clear the overlap batch (the dispatcher self-clears);
  it only resets per-entity `C_ContactEvent`. Keep it in the pipeline for
  canonical parity, not because the overlap assertions need it.
- This is a pure test addition — no engine behavior change, so no
  render/screenshot verification and no `optimize` pass apply.
