# Plan: ECS world-reset / teardown primitive for scene transitions

- **Issue:** #1814
- **Model:** opus
- **Date:** 2026-06-14
- **Decomposition:** one opus PR (rationale below)

## Verified current state (what exists, what's missing)

Grounded in `engine/entity`, `engine/system`, `engine/world`:

**Entity destruction**
- `IREntity::destroyEntity(EntityId)` — `engine/entity/include/irreden/ir_entity.hpp:177`,
  impl `engine/entity/src/entity_manager.cpp:125`. Fires pre-destroy hooks (registration
  order) + each component's `onDestroy()`, swap-removes from the archetype node, returns the
  id to the atomic counter. Eager, main-thread only.
- `IREntity::destroyAllEntities()` — `entity_manager.cpp:351`. Flushes deferred changes,
  snapshots live ids from `m_entityIndex`, destroys **every** entity (singletons included),
  then clears the singleton cache `m_singletonEntityByComponent` at the end.
- **GAP:** no *subset/filtered* destroy. `destroyAllEntities` is all-or-nothing and takes
  singletons with it.

**Singletons — the preserve set already has a registry**
- `IREntity::singleton<T>()` / `singletonEntity<T>()` — `ir_entity.hpp:280`. Singleton
  entities are *normal* entities in the archetype graph, cached by `ComponentId` in
  `EntityManager::m_singletonEntityByComponent` (entity `CLAUDE.md` "Singleton components").
  **This cache is already the authoritative registry of which entity ids are singletons** —
  reuse it as the preserve set; no new bookkeeping needed.
- Must-survive examples: `C_GlobalModifiers`, the sim-clock `C_SimClock` (#200), future
  game-rules holders.

**Gameplay vs engine tagging**
- Tag components exist (`C_*Tag{}`) but only as *system-dispatch filters* (`Exclude<Tag>`),
  never lifecycle/group markers. **GAP:** no "gameplay"/"scene" grouping today.

**System pipelines**
- `SystemManager::registerPipeline(event, list)` — `system_manager.hpp:186` — **replaces**
  the event's pipeline (so swap-by-re-register already works). `appendToPipeline` /
  `insertIntoPipelineBefore/After` compose; `executePipeline(event)` runs it.
- **GAP:** no clear/unregister convenience; no "destroy system" (systems persist for the
  manager lifetime — dropping one from a pipeline just makes it inert, which is acceptable
  since the set is bounded); no "which systems are active" query.

**Render / GL-Metal context**
- The device/context lives on the RenderManager (manager state), NOT as entities (entity
  `CLAUDE.md` "Use the API for what it's for") → it survives a partial entity teardown
  automatically.
- Most render-bearing components free their GPU resources in `onDestroy()` — e.g.
  `C_GPUParticlePool::onDestroy()` (`component_gpu_particle_pool.hpp:79`),
  `C_VoxelSetNew::onDestroy()` → `VoxelPool::deallocate` (`component_voxel_set.hpp:288`),
  the canvas texture/shadow/AO/light-volume components. Destroying a scene's render entities
  frees their GPU memory with no extra code, idempotently (guards like `numVoxels_ > 0`).
  **Exception: `C_Sprite` (`component_sprite.hpp:37`) holds `textureHandle_: IRRender::ResourceId`
  with no `onDestroy()`** — sprite entities in gameplay scenes leak texture ResourceIds on reset.
  Audit for additional missing cases at implementation time.

**Lua surface**
- `IRSystem.registerSystem{...}` / `IRSystem.registerPipeline(event,{ids})` exposed —
  `lua_pipeline_bindings.hpp`. `IRTime.UPDATE/RENDER/INPUT/...` already an enum table
  (`lua_pipeline_bindings.hpp:62`). `IREntity.singleton(componentDef)` exposed (entity
  `CLAUDE.md:223`). **GAP:** no Lua reset / pipeline-clear.

## Approach (one approach, picked)

Add a **preserve-by-default reset**: destroy every live entity *except* (1) singleton
entities and (2) entities explicitly marked persistent. This reuses the singleton cache as
the preserve registry (zero per-gameplay-entity bookkeeping — the common case "just works"),
with `C_Persistent{}` as the escape hatch for the rare non-singleton entity an
engine/creation needs to survive. Pipeline swap reuses the existing `registerPipeline`
replace semantics; add only a thin `clearPipeline` convenience and the Lua glue.

Steps, in order:

1. **`C_Persistent{}` marker** — new empty tag component (place alongside existing common
   markers, e.g. `engine/prefabs/irreden/common/`). Opt-out: an entity carrying it is spared
   by reset. Singletons are spared automatically and need no tag.
   **RenderManager must stamp `C_Persistent{}` on each of its 4 canvas entities**
   (`m_mainFramebuffer`, `m_mainCanvas`, `m_backgroundCanvas`, `m_guiCanvas`, per
   `engine/render/src/render_manager.cpp:136-141`) immediately after creating them at startup.
   These are non-singleton entities; without the tag they are destroyed on the first scene reset,
   breaking the renderer.

2. **Core primitive in `EntityManager`** — `destroyAllExceptPreserved()` (working name; e.g.
   `resetGameplayEntities`):
   - `flushStructuralChanges()` first (mirror `destroyAllEntities`).
   - Build the preserve set: all `EntityId`s in `m_singletonEntityByComponent` ∪ all ids
     matching `C_Persistent` (`forEachComponent<C_Persistent>`).
   - Snapshot live ids from `m_entityIndex`; `destroyEntity(id)` for every id NOT in the
     preserve set.
   - **Do NOT clear `m_singletonEntityByComponent`** — singletons survive, so the cache
     stays valid (the key difference from `destroyAllEntities`).
   - Eager, main-thread, snapshot-based — safe outside iteration; must NOT be called mid
     `forEachComponent`.

3. **Public facade** — `IREntity::resetGameplay()` free function (`ir_entity.hpp` /
   `entity_manager.cpp`) forwarding to the manager method.

4. **Pipeline swap support** — `SystemManager::clearPipeline(event)` = `registerPipeline(event,
   {})` convenience (`system_manager.hpp`/`.cpp`); expose `IRSystem::clearPipeline`. Document
   that scene swap = `clearPipeline`/`registerPipeline` per event. Systems persisting in
   memory is acceptable; the scene machine just re-points pipelines.
   After composing the new scene's pipeline, call `revalidatePipelines()` — `validateAllPipelineGroups()`
   runs only at `World::start()` (`world.cpp:268-277`); a mid-run `registerPipeline` skips
   revalidation and multi-system group ordering may be silently wrong.

5. **Lua surface** — extend `lua_pipeline_bindings.hpp`: `IRWorld.resetGameplay()` (or
   `IREntity.resetGameplay()`) + `IRSystem.clearPipeline(event)`. No new enum required
   (`IRTime` events already a table); honor the enum-table rule if a preserve-policy enum is
   added later.

6. **Ordering contract** — the scene machine must, within one frame boundary:
   `resetGameplay()` → register the next scene's pipelines → spawn the next scene's entities,
   so no system ticks against destroyed entities and the new scene is live next tick.
   Document in engine `CLAUDE.md` + the demo.

7. **Demo + idempotency test** — `creations/demos/scene_reset/` (or extend an existing demo):
   build scene A (canvas + voxels + a system), `resetGameplay()`, build scene B, loop N≥10
   cycles. Assert: live-entity count returns to the preserve-set baseline each cycle (no
   gameplay leak); VoxelPool allocated-voxel count returns to baseline (CPU/GPU voxel memory
   freed); the engine still renders after reset (context survived). Runs headless under
   `--auto-screenshot`.

## Affected files
- `engine/prefabs/irreden/common/.../component_persistent.hpp` (new) — `C_Persistent{}`.
- `engine/entity/include/irreden/entity/entity_manager.hpp` + `src/entity_manager.cpp` —
  `destroyAllExceptPreserved()`.
- `engine/entity/include/irreden/ir_entity.hpp` — `IREntity::resetGameplay()` facade.
- `engine/system/include/irreden/system/system_manager.hpp` + `src/system_manager.cpp` —
  `clearPipeline(event)`.
- `engine/system/include/irreden/ir_system.hpp` — `IRSystem::clearPipeline` facade.
- `engine/script/include/irreden/script/lua_pipeline_bindings.hpp` — `IRWorld.resetGameplay()`
  + `IRSystem.clearPipeline`.
- `engine/render/src/render_manager.cpp` — stamp `C_Persistent{}` on the 4 canvas entities.
- `engine/entity/CLAUDE.md` + `engine/system/CLAUDE.md` — document the primitive, preserve-set
  semantics, ordering contract, dangling-id hazard.
- `creations/demos/scene_reset/` (new) — demo + idempotency assertions; CMake + pipeline
  registration (create-creation conventions).

## Acceptance criteria
- A demo builds a scene, `resetGameplay()`s, and builds a different scene ≥10× with **no
  entity-count growth** above the preserve-set baseline and **no VoxelPool/GPU growth** across
  cycles.
- Singletons (`C_GlobalModifiers`, `C_SimClock`) and the render/GL-Metal context survive a
  reset; the engine renders the next scene correctly.
- No system ticks against destroyed entities (reset + re-register happen at a frame boundary).
- The primitive is callable from Lua (`IRWorld.resetGameplay()`) so game #190's scene machine
  can drive it.
- Builds + runs headless on macOS (Metal) and Linux (GL).

## Gotchas
- **Dangling EntityIds across a reset.** Surviving singletons/persistent entities holding an
  `EntityId` of a destroyed gameplay entity go stale. The pre-destroy hook auto-sweeps
  *modifiers* only — arbitrary id fields are not swept. Re-acquire ids after rebuild, or null
  them in a pre-destroy hook.
- **Call at a frame boundary, not mid-iteration.** The primitive eager-destroys on a snapshot
  (mirrors `destroyAllEntities`); calling it inside a `forEachComponent` / parallel group is
  UB. Drive it from a command or a standalone transition system.
- **Do NOT clear the singleton cache** (unlike `destroyAllEntities`) — singletons survive;
  clearing would orphan their live rows (entity `CLAUDE.md` "lazy-create-ghost" hazard,
  lines 177-188).
- **Pre-destroy hooks during partial teardown** must use the no-create singleton accessors
  (`singletonOrNull`) — same rule as bulk teardown.
- **System-internal / Lua-global state persists** across reset (systems are never destroyed;
  the Lua VM isn't reset). Keep per-scene state in components (destroyed on reset), not in
  system statics or Lua globals; the scene machine owns its own Lua state.
- **Which engine entities need `C_Persistent`?** Any engine-created *non-singleton* entity
  that must survive must be tagged, or it is torn down. **Concretely: the RenderManager's 4
  canvas entities (`m_mainFramebuffer`, `m_mainCanvas`, `m_backgroundCanvas`, `m_guiCanvas`,
  `render_manager.cpp:136-141`) are non-singleton and must be stamped `C_Persistent{}` at
  construction** — without the tag they are destroyed on the first scene reset, breaking the
  renderer. Verify during implementation by asserting the engine still renders after a reset.
- **`m_namedEntities` must be pruned after reset.** `destroyEntity()` does not remove entries
  from `EntityManager::m_namedEntities` (`entity_manager.cpp:179-188`). After
  `destroyAllExceptPreserved()`, any gameplay entity registered via `setName("camera")` retains
  a stale name→id entry; a subsequent `getEntityByName("camera")` asserts at line 187 on the
  dead id. After the destroy loop, erase every entry in `m_namedEntities` whose id no longer
  exists in `m_entityIndex`.
- **Idempotency measurement.** Entity ids never recycle (atomic counter), so don't assert on
  id *values* — assert on live-entity *count* and on resource counters (VoxelPool) returning
  to baseline.

## Sibling / in-flight reconciliation
- **Consumer:** game #190 "Scene/state machine Lua module" (planned in this batch) is the
  direct Lua consumer of `resetGameplay()`. This ticket is the engine half; keep the Lua name
  stable for #190.
- **Adjacent, distinct:** #199 world-snapshot save (designed-not-implemented) is persistence,
  not teardown — no overlap. Both touch `destroyAllEntities`/the singleton cache, but this
  plan does **not** change `destroyAllEntities` (only adds a sibling method), so #199 is
  unaffected.
- No open PR currently touches `entity_manager` teardown or `system_manager` pipeline
  registration — no in-flight conflict.

## Cross-system audit (lifecycle-touching addition)
- **Singleton cache** — `destroyAllExceptPreserved` reads `m_singletonEntityByComponent` but
  must NOT clear it (only `destroyAllEntities` clears it, at end-of-world). No other consumer
  affected.
- **Pre-destroy hooks** — fire normally per destroyed entity (modifier-sweep hook etc.); no
  change to hook registration.
- **Component `onDestroy()`** — every render/voxel component with GPU resources already
  implements it; the reset relies on this existing contract (no per-component change).
- **`registerPipeline` consumers** — `clearPipeline` is additive (= `registerPipeline(event,
  {})`); existing pipeline registrations unaffected.
- **No public symbol removed** → Engine API removal rule N/A.
