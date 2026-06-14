# Plan: ECS world-reset / teardown primitive for scene transitions

- **Issue:** #1814
- **Model:** opus
- **Date:** 2026-06-14
- **Epic:** game-repo `jakildev/irreden#192` (GMTK '26 jam-readiness), build-order step 1/5
- **Hard downstream:** game `jakildev/irreden#190` (scene/state machine) is **Blocked by #1814**

## Scope

Add an engine primitive that tears down a running gameplay scene and lets the
next scene be rebuilt in-place, repeatably, without leaking entities or GPU
resources and without killing engine-level state (render/GL/Metal context,
C++ managers, designated must-survive entities). This is the structural
backbone the game's Lua scene/state machine (#190) drives. It is **teardown
only** — persistence (world-snapshot save #199/#667) is explicitly out of scope.

The deliverable is a single coherent **capability contract** with four faces,
because the consumer (#190) cannot start until the whole contract exists:
1. gameplay-entity teardown that preserves engine entities + managers,
2. system-pipeline swap/clear,
3. automatic GPU-resource release tied to entity destruction (no leaks),
4. a Lua-reachable surface for 1–3, honoring the enum-as-Lua-table convention.

## Verified current state (confirmed against the code, not the body's guesses)

- **Entity pool:** `EntityManager` owned per-`World`
  (`engine/world/include/irreden/world.hpp:47`); singleton ptr `g_entityManager`.
  Not process-global — each World owns one.
- **Only existing bulk teardown:** `IREntity::destroyAllEntities()`
  (`engine/entity/src/entity_manager.cpp:351-372`) destroys **literally every**
  entity and clears the singleton cache `m_singletonEntityByComponent`. It is
  **test-only** today (`test/ecs/entity_manager_test.cpp:233`); no production
  caller. There is **no subset/preserve teardown** and **no
  gameplay-vs-engine tag** anywhere.
- **destroyAllEntities footguns (verified):** it does **not** prune
  `m_namedEntities` (`entity_manager.hpp:475`) → a name like `"camera"` keeps
  pointing at a destroyed id; does **not** reset the monotonic
  `m_nextEntityId{IR_RESERVED_ENTITIES}` (`:501`) — fine for correctness (no id
  reuse/collision), only consumes id space; does **not** drain
  `m_preDestroyHooks` (`:502`) or the archetype graph (empty nodes persist).
- **Engine singletons that survive for free:** the C++ managers
  (`RenderManager`, `AudioManager`, `RenderingResourceManager`, `TimeManager`,
  `VideoManager`) and the Lua state are `World` members, **not entities**
  (`world.hpp:41-59`) → any entity teardown leaves them intact automatically.
- **Engine entities that DO need preserving:** `RenderManager`'s four canvases
  are real ECS entities — `m_mainFramebuffer`, `m_mainCanvas`,
  `m_backgroundCanvas`, `m_guiCanvas`
  (`engine/render/include/irreden/render/render_manager.hpp:136-141`), created
  via `createEntity<...>` at construction. A naive `destroyAllEntities()` would
  destroy these and leave RenderManager holding dangling ids. **These are the
  canonical must-preserve set.**
- **Pre-destroy hook mechanism exists:** `registerPreDestroyHook(PreDestroyHook)`
  / `unregisterPreDestroyHook` (`entity_manager.hpp:429-430`; `PreDestroyHook =
  std::function<void(EntityId)>`, `:36`). Hooks fire **before** component
  teardown in `destroyEntity` (`entity_manager.cpp:131-135`). This is the clean
  seam for automatic GPU-resource release.
- **GPU resources are manual, not RAII, not entity-tied:**
  `RenderingResourceManager` pools `unique_ptr<T>` by `ResourceId`
  (`engine/render/include/irreden/render/rendering_rm.hpp:37-52`); `create<T>` /
  `destroy<T>(id)` are explicit. Destroying a voxel/canvas entity does **not**
  free its `ResourceId`s today → that is the "GPU leak across cycles" gap the
  acceptance criteria forbid. The render context (`RenderImpl`, GL/Metal) is a
  separate `unique_ptr` (`render_manager.hpp:128`) and survives entity teardown.
- **System pipelines (verified, `engine/system/src/system_manager.cpp`):**
  systems are append-only — a `SystemId` slot is never freed; there is **no
  destroySystem**. Pipeline APIs: `registerPipeline(event, ids)` (:106-117) and
  `registerPipelineGroups` (:120-125) **replace** the whole event's list;
  `appendToPipeline` / `insertIntoPipelineBefore/After` (:143-209, PR #1540) add
  a single system non-destructively. There is **no clear/swap/deactivate**.
  `validateAllPipelineGroups()` runs **once** at `World::start()`
  (`world.cpp:268-277`) and is **not** re-run after later pipeline changes.
- **Tick safety:** `executeSystem` iterates `node->length_` at dispatch time
  (`system_manager.cpp:391-502`) and `flushStructuralChanges` runs between
  groups — so destroyed entities simply vanish from the next tick; systems do
  **not** dangle on stored handles. The real dangle risk is a system caching a
  *foreign* entity id (e.g. a camera) in `beginTick`/params across a reset.
- **Lua bindings:** central registration in
  `engine/script/src/lua_script.cpp:373-642`
  (`bindLuaDrivenEcs` / `bindLuaDrivenSystems` / `bindLuaCommands` / `bindSimApi`).
  Enum-as-Lua-table convention via the `IR_BIND_ROTMODE` macro / `IREnum.register`
  (`lua_script.cpp:472-477`; rule `.claude/rules/cpp-lua-enums.md`). Pipeline
  composition is already Lua-exposed
  (`engine/script/include/irreden/script/lua_pipeline_bindings.hpp:210-334`).
- **Demos are single hardcoded scenes:**
  `init → initSystems → initCommands → initEntities → gameLoop`
  (`creations/demos/default/main.cpp:37-151`); no mid-run teardown anywhere.

## Approach (one approach, committed)

**Model: a destroy-by-default preserve-set, not a tag.** Teardown destroys
**every** entity *except* an explicitly-registered preserve set. This is chosen
over an opt-in "gameplay tag" because the failure modes invert correctly: an
unregistered new entity is *destroyed* (loud, immediate breakage — caught at
once), whereas an untagged gameplay entity under the tag model would *survive*
and silently grow entity count across cycles — exactly the leak the acceptance
criteria forbid. Destroy-by-default also needs no migration of existing
creations (nothing has to be tagged).

Implement in this order (one PR):

1. **EntityManager preserve-set + reset (engine/entity).**
   - Add `std::unordered_set<EntityId> m_preservedEntities;` and public
     `preserveEntity(EntityId)` / `releaseEntity(EntityId)` (idempotent;
     mask with `IR_ENTITY_ID_BITS` consistent with the existing code).
   - Add `void EntityManager::resetWorld();` (and `IREntity::resetWorld()` in
     `ir_entity.hpp`) that mirrors `destroyAllEntities()` but: skips ids in
     `m_preservedEntities`; after the destroy loop, **prunes `m_namedEntities`**
     of any entry whose id no longer exists; clears
     `m_singletonEntityByComponent` (so ECS singletons lazily re-mint fresh).
     Do **not** reset `m_nextEntityId` (preserved entities hold low ids; resetting
     the counter would collide). Factor the shared destroy loop so
     `destroyAllEntities()` becomes `resetWorld()` with an empty preserve set (or
     a private `destroyEntitiesExcept(set)` both call) — keep `destroyAllEntities`
     behavior byte-for-byte for its existing test.
2. **RenderManager registers its must-survive entities + GPU cleanup hook
   (engine/render).**
   - After creating the four canvases, call `IREntity::preserveEntity(id)` for
     each (`m_mainFramebuffer/m_mainCanvas/m_backgroundCanvas/m_guiCanvas`).
   - Register **one** pre-destroy hook at RenderManager construction (store the
     `PreDestroyHookId`, unregister in the dtor) that, for the entity being
     destroyed, checks for the GPU-resource-bearing render components (voxel pool
     / canvas-texture components — grep `ResourceId` fields under
     `engine/prefabs/irreden/render/components/` and `C_VoxelPool`) and calls
     `RenderingResourceManager::destroy<T>(id)` for each held `ResourceId`. This
     makes GPU release **automatic and entity-lifetime-tied** for *all* entity
     destruction, not just resets — directly satisfying "no leaked GPU
     resources". (A `getComponent` inside a destroy hook is fine — teardown, not
     a hot tick.)
3. **Pipeline swap/clear (engine/system).**
   - Add `SystemManager::clearPipeline(IRTime::Events)` (empties that event's
     groups) and a public `revalidatePipelines()` that re-runs
     `validateAllPipelineGroups()`. The swap contract for the game: call
     `clearPipeline(event)` then `registerPipeline(event, sceneB_ids)` (or just
     `registerPipeline`, which already replaces) for each event, then
     `revalidatePipelines()` once. Confirm `registerPipeline(event, {})` yields a
     no-system (paused) pipeline — add the empty-list path if missing.
4. **Lua surface (engine/script).**
   - New `engine/script/include/irreden/script/lua_world_bindings.hpp` with
     `detail::bindWorldManagement(LuaScript&)`, called from `lua_script.cpp`
     near line 638. Expose `IRWorld.resetWorld()`, `IRSystem.clearPipeline(event)`
     / `IRSystem.revalidatePipelines()` (alongside the existing pipeline
     bindings). Any scene/pipeline identifiers that cross the boundary must use
     the enum-as-Lua-table pattern (`IREnum.register` / `IR_BIND_*`), never
     string-name lookups — #190 registers its TITLE/PLAY/PAUSE/GAME_OVER set this
     way and expects engine ids in the same shape.
5. **Idempotency test (test/ecs + optional Lua demo).**
   - C++ test: preserve a sentinel entity, build a scene (N entities, some with
     GPU `ResourceId`s), `resetWorld()`, rebuild — repeat ≥3×. Assert: live
     entity count returns to the post-build baseline each cycle (no growth);
     preserved sentinel + (mock) canvas ids survive; a destroyed named entity is
     gone from `m_namedEntities`; singleton re-mints a fresh id; the
     RenderingResourceManager `ResourceId` count returns to baseline (no GPU
     leak). Optionally a tiny Lua reset→rebuild loop in a demo to prove the bound
     surface, but the C++ test is the gate.

## Affected files

- `engine/entity/include/irreden/entity/entity_manager.hpp` — preserve-set members, `preserveEntity`/`releaseEntity`/`resetWorld` decls.
- `engine/entity/src/entity_manager.cpp` — `resetWorld` + shared destroy loop + `m_namedEntities` prune.
- `engine/entity/include/irreden/ir_entity.hpp` — public `IREntity::resetWorld/preserveEntity/releaseEntity` forwards.
- `engine/render/include/irreden/render/render_manager.hpp` / `src/render_manager.cpp` — preserve the 4 canvases; register/unregister the GPU-cleanup pre-destroy hook.
- `engine/system/include/irreden/system/system_manager.hpp` / `src/system_manager.cpp` — `clearPipeline` + `revalidatePipelines` (+ empty-list `registerPipeline` path if needed).
- `engine/script/include/irreden/script/lua_world_bindings.hpp` (new) + `engine/script/src/lua_script.cpp` — `bindWorldManagement` wiring.
- `test/ecs/` — idempotency test (new file or extend `entity_manager_test.cpp`).
- `engine/entity/CLAUDE.md` + `engine/system/CLAUDE.md` — document the preserve/reset + pipeline-swap contract.

## Cross-system audit (shared resource: the entity pool + GPU resource pool)

The change is **additive** — it does not migrate existing consumers:
- `destroyAllEntities()` keeps identical behavior (its only caller is a test);
  `resetWorld()` is new. No existing caller changes semantics.
- The preserve-set is opt-in: only RenderManager registers today. **Contract to
  document:** any engine subsystem that creates a must-survive entity (future
  audio/video singletons, editor chrome) must call `preserveEntity` at creation —
  otherwise `resetWorld()` destroys it. Grep audited for entity-holding engine
  managers: RenderManager (4 canvases) is the only current case;
  Audio/Video/Time managers hold no EntityId members.
- The GPU pre-destroy hook reads render components by presence; it must cover
  **every** component that owns a `ResourceId` — enumerate them at implementation
  time by grepping `ResourceId` under `engine/prefabs/irreden/render/components/`
  and `engine/render/`, and free each. Missing one re-introduces the leak.

## Acceptance criteria

- Build → `resetWorld()` → rebuild a *different* scene N≥3× with **no live
  entity-count growth** and the RenderingResourceManager `ResourceId` count back
  to baseline each cycle (**no GPU leak**).
- Preserved entities (RenderManager canvases / a test sentinel) and all C++
  managers + render context survive every reset; only non-preserved entities die.
- A pipeline swap re-points cleanly: scene B's systems tick, scene A's do not,
  and no system ticks against a destroyed entity; `revalidatePipelines()` passes.
- `resetWorld`, pipeline clear/swap, and revalidate are all callable from Lua,
  with any crossing identifiers as enum-as-Lua-table values.

## Gotchas

- **Do NOT reset `m_nextEntityId`.** Preserved entities hold low ids minted at
  boot; resetting the counter to `IR_RESERVED_ENTITIES` would collide with them.
  Monotonic growth is correct; the 32-bit id space (~33M) dwarfs any jam reset
  count. (If a future use truly needs id-space reclaim, that is a separate task.)
- **Prune `m_namedEntities`** during reset — the single nastiest existing
  footgun: a stale `"camera" → dead id` survives `destroyAllEntities` today and
  would make `getEntity("camera")` assert post-reset.
- **Foreign-handle caches dangle.** A system that caches a non-preserved entity
  id (camera, player) in `beginTick`/params will dangle after a reset. Mitigate
  by re-establishing such caches in the new scene's init; note in the CLAUDE.md
  contract. (A one-shot "pre-reset" hook to let subsystems null caches is a
  reasonable *follow-up* if real cases appear — not in this PR's scope.)
- **GPU hook must be exhaustive.** Enumerate *all* `ResourceId`-bearing render
  components; a missed one silently re-leaks and fails the acceptance test.
- **Archetype-graph empty nodes** are not pruned (same as today). Not a leak of
  entities/GPU; acceptable for v1. Don't add graph compaction here.
- **Pipeline validation** is not auto-run on swap — the game MUST call
  `revalidatePipelines()` after composing scene B, or a mis-composed multi-system
  group races undetected. Single-system groups (the likely jam case) pass
  trivially, but expose + document the call regardless.
- **One task, not a stack:** the four faces are one contract #190 consumes
  whole; splitting would create micro-PRs that only make sense merged together
  and need file-epic + re-approval. If the GPU-cleanup face genuinely balloons
  the diff past one reviewable PR, use the normal step-8 follow-up escalation —
  do not pre-split.
