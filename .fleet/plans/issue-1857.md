# Plan: demo: scene_reset — render-stack idempotency proof for resetGameplay (VoxelPool + render-survival)

- **Issue:** #1857
- **Model:** opus
- **Date:** 2026-06-19
- **Decomposition:** one opus PR (single new demo)
- **Blocked by:** #1814 (MERGED/COMPLETED — primitive landed)

## Scope

The render-stack half of #1814's acceptance that a headless unit test cannot
exercise: a `creations/demos/scene_reset/` demo that drives
`IREntity::resetGameplay()` + pipeline swap over a **live render context**,
rebuilds a different scene, loops N≥10 cycles, and asserts that neither the
live-entity count nor the VoxelPool grows across cycles — proving the engine's
GPU/voxel teardown is idempotent and the renderer survives every reset. #1814
proved the entity-side contract via unit tests; this is the engine-side proof
over a real `RenderManager` + `C_VoxelPool`.

## Verified current state (premise confirmed against origin/master)

The #1814 primitive landed; every API the demo needs exists and is verified:

- `IREntity::resetGameplay()` — `engine/entity/include/irreden/ir_entity.hpp:187`
  (impl `engine/entity/src/ir_entity.cpp:19`). Preserve-by-default teardown:
  singletons + `C_Persistent` + component-backing entities survive; the 4
  RenderManager canvas entities are stamped `C_Persistent` at construction
  (`engine/render/src/render_manager.cpp`), so the render context survives.
- `IRSystem::clearPipeline(IRTime::Events)` — `engine/system/include/irreden/ir_system.hpp:474`
  (= `registerPipeline(event, {})`). `IRSystem::registerPipeline(event, list)`
  *replaces* the event's pipeline — `ir_system.hpp:428`.
- `IRSystem::validateAllPipelineGroups()` — `ir_system.hpp:468`. Runs once at
  `World::start()`; a **mid-run** `registerPipeline` does NOT re-trigger it, so
  the demo must call it after each scene swap (`ir_system.hpp:434` says so
  explicitly; #1814 plan step 4).
- `IREntity::getLiveEntityCount()` → `EntityId` — `ir_entity.hpp:39`, backed by
  `EntityManager::m_liveEntityCount` (`entity_manager.hpp:425`). Incremented on
  create (`entity_manager.cpp:49`), **decremented on destroy**
  (`entity_manager.cpp:104`) — so it genuinely falls back to the preserve-set
  baseline after a reset.
- `C_VoxelPool::getLiveVoxelCount()` → `int` — `component_voxel_pool.hpp:235`
  (returns `m_voxelPoolIndex`). `getVoxelPoolSize()` — `:232`.
- `IRPrefab::VoxelPool::detail::poolForCanvas(EntityId canvas)` → `C_VoxelPool*`
  (null-safe) — `engine/prefabs/irreden/voxel/voxel_pool_api.hpp:103`.
- `IRRender::getActiveCanvasEntity()` — `engine/render/include/irreden/ir_render.hpp:143`
  (null-safe `getActiveCanvasEntityOrNull()` in `render/active_canvas.hpp:16`).
- `C_VoxelSetNew::onDestroy()` — `component_voxel_set.hpp:286` — calls
  `IRPrefab::VoxelPool::deallocate(...)` guarded by `numVoxels_ > 0`. This is the
  hook `resetGameplay` relies on to free voxel memory.
- Auto-screenshot: `IRVideo::parseAutoScreenshotArgv(argc, argv, &warmupFrames)`
  + `IRVideo::createAutoScreenshotSystem(AutoScreenshotConfig)` —
  `engine/video/include/irreden/video/auto_screenshot.hpp:106,122,88`. Model the
  wiring on `creations/demos/shape_debug/main.cpp`.

### The load-bearing subtlety: `getLiveVoxelCount()` is a HIGH-WATER MARK

This drives the entire assertion design, so it is verified in detail:

- `C_VoxelPool::allocateVoxels(size)` first tries `findFreeSpan(size)`; on miss it
  **bump-allocates** `m_voxelPoolIndex += size` (`component_voxel_pool.hpp:148-150`).
- `deallocateVoxels(start, size)` zeros colors / clears the active mask / nulls
  entity ids and pushes the span onto a free list
  (`m_freeVoxelSpans` + `m_freeSpanLookup[size]`, `:198-200`) — it **never
  lowers `m_voxelPoolIndex`**.
- `findFreeSpan(req)` does `m_freeSpanLookup.lower_bound(req)` then returns the
  span **only if `span.second <= req`** (`:750-759`). Since `lower_bound` already
  guarantees `size >= req`, the span is returned **iff its size == req** — i.e.
  reclaim is **exact-size only, with no splitting**.

Consequence: `getLiveVoxelCount()` (= `m_voxelPoolIndex`) is the allocator's
**high-water mark**, not a live-occupancy count. It returns to / stays at
baseline across reset cycles **only when each rebuild re-requests the exact
allocation sizes the previous teardown freed**. A genuine leak (e.g. `onDestroy`
not firing) makes it grow **unboundedly** every cycle — which is exactly the
signal we want to assert against. There is **no public live-occupancy accessor**
(the free-list members are private), so "no growth in the high-water mark across
cycles" is the correct and available leak signal — NOT "== 0 after reset"
(unreadable and false).

## Approach (one approach, picked)

A C++-only demo whose two scenes have **identical voxel-allocation footprints**
(so the high-water mark stays strictly flat once the free list is warm), differ
visually + in pipeline composition (so `clearPipeline`/`registerPipeline` is
genuinely exercised), and whose driver runs the reset loop at a frame boundary
and asserts count idempotency every cycle.

1. **New demo dir** `creations/demos/scene_reset/` — `main.cpp` + `CMakeLists.txt`,
   C++-only (no Lua). Target `IRSceneReset`.

2. **CMake (modern idiom).** Use `irreden_bundle_assets(IRSceneReset)` +
   `irreden_package_target(IRSceneReset)` — **NOT** the hand-rolled
   `copy_directory` template still shown in the `create-creation` SKILL.md, which
   #1888/#1896 migrated 22 demos away from. Model the file on a recent
   C++-only migrated demo (or `shape_debug/CMakeLists.txt`, dropping its
   `SCRIPTS config.lua` arg since this demo has no Lua config). Register the new
   subdir in `creations/CMakeLists.txt` via
   `add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/demos/scene_reset)` — without the
   parent entry the target silently does not build.

3. **Two scenes, matched footprint.** `buildSceneA()` and `buildSceneB()` each
   create the **same number of voxel-set entities with the same `C_VoxelSetNew`
   dimensions** (e.g. each a single solid cube of identical size S). They differ
   only in (a) color, (b) position/offset, and (c) **which UPDATE system animates
   them** — Scene A driven by one system, Scene B by a different one — so the
   per-scene pipeline swap is a real reconfiguration, not a no-op. (The identical
   voxel dimensions are required; see Gotchas.)

4. **Scene driver = a dedicated transition hook** that owns the cycle loop. Keep
   its state (frame counter, cycle index, captured baselines) in `static`s or a
   singleton/`C_Persistent` entity — **never a gameplay entity** (reset would
   wipe it). Positioned **last in the UPDATE pipeline**, its per-frame end hook
   (outside any `forEachComponent` — a frame boundary) every `kFramesPerScene`
   frames performs ONE transition:
   a. `IREntity::resetGameplay()` — tear down the current scene.
   b. `IRSystem::clearPipeline(IRTime::Events::UPDATE)` then
      `IRSystem::registerPipeline(IRTime::Events::UPDATE, {driver, ...nextScene})`
      — re-include the driver so it persists across the swap.
   c. `IRSystem::validateAllPipelineGroups()` — revalidate after the mid-run swap.
   d. `buildSceneA()` / `buildSceneB()` alternating — spawn the next scene.
   RENDER runs after UPDATE, so it draws the rebuilt scene the same frame.

5. **Per-cycle assertions.** Resolve the pool once per check via
   `IRPrefab::VoxelPool::detail::poolForCanvas(IRRender::getActiveCanvasEntity())`.
   - **Warm-up then baseline:** run 1 full reset+rebuild cycle first (lets lazy
     component-backing entities instantiate and seeds the free list for both
     scenes' sizes), THEN capture `baselineLive = getLiveEntityCount()` and
     `baselineVox = pool->getLiveVoxelCount()` right after a rebuild, plus
     `postResetBaseline = getLiveEntityCount()` right after a `resetGameplay()`.
   - **Post-reset invariant (no gameplay leak):** after each `resetGameplay()`,
     `getLiveEntityCount() == postResetBaseline`. Growth ⇒ gameplay entities not
     destroyed.
   - **Post-rebuild invariant (deterministic + no pool growth):** after each
     rebuild, `getLiveEntityCount() == baselineLive` AND
     `pool->getLiveVoxelCount() == baselineVox`. Strict equality both.
   - **On mismatch:** `IR_LOG_ERROR("FAIL cycle N: entities X->Y voxels A->B")`
     then `std::abort()` (non-zero exit; a headless harness/CI catches it).
   - **On pass through kCycles (≥10; use 12):** `IR_LOG_INFO("PASS: N cycles, no
     entity/voxel leak")` and `IRWindow::closeWindow()`.

6. **Render-survival + headless.** Support `--auto-screenshot`
   (`parseAutoScreenshotArgv` → append `createAutoScreenshotSystem(cfg)` to
   RENDER, like shape_debug). The **driver owns the exit** (closeWindow after
   kCycles); size the auto-screenshot warmup/shot schedule so at least one shot of
   Scene A and one of a post-reset Scene B are captured before the driver closes,
   giving visual proof the renderer survived ≥1 reset. The programmatic proof is
   that `getActiveCanvasEntity()` stays valid and rendering never crashes across
   all resets.

## Affected files
- `creations/demos/scene_reset/main.cpp` (new) — driver + `buildSceneA/B` +
  per-cycle assertions + auto-screenshot wiring.
- `creations/demos/scene_reset/CMakeLists.txt` (new) — `IRSceneReset` via
  `irreden_bundle_assets` + `irreden_package_target`.
- `creations/CMakeLists.txt` — `add_subdirectory(.../demos/scene_reset)`.

## Acceptance criteria
- `IRSceneReset` builds clean on the host preset; runs headless under
  `--auto-screenshot` and exits 0 when idempotent.
- ≥10 reset/rebuild cycles. `getLiveEntityCount()` returns to the post-reset
  preserve-set baseline each cycle, and the post-rebuild count is constant.
- `C_VoxelPool::getLiveVoxelCount()` is constant across cycles after the warm-up
  build (no unbounded VoxelPool growth) — given the matched scene footprints.
- The renderer survives each reset (canvas + camera persist via `C_Persistent`);
  screenshots are captured across at least two scenes.
- Builds + runs headless on both macOS (Metal) and Linux (GL) — the demo exercises
  the dual-backend voxel→trixel path.

## Gotchas
- **`getLiveVoxelCount()` is a high-water mark, not a live count** (verified
  above). Scene A and B **must** allocate identical voxel-set sizes, or the index
  grows on a size mismatch *even with a correct reset* (false leak). Assert "no
  growth across cycles," never "== 0 after reset."
- **Drive the transition at a frame boundary, never mid-tick.** `resetGameplay`
  eager-destroys on a snapshot; calling it inside a `forEachComponent` / parallel
  group is UB (#1814 CLAUDE.md). The driver's per-frame end hook is safe; never
  reset from a per-entity tick.
- **Re-include the driver in UPDATE every swap** (`clearPipeline` replaces the
  whole event) and call `IRSystem::validateAllPipelineGroups()` after — a mid-run
  `registerPipeline` skips `World::start()`'s validation, so parallel-group
  ordering can be silently wrong (`ir_system.hpp:434`).
- **Driver/cycle state must live in statics or a singleton/`C_Persistent` entity**,
  not a gameplay entity — reset destroys gameplay state (would reset the counter
  every cycle).
- **Idempotency is a count, not ids.** Entity ids never recycle (atomic counter);
  assert on counts/resource counters, never on id values (#1814 CLAUDE.md).
- **CMake:** use `irreden_bundle_assets` (not the stale SKILL.md hand-rolled
  copy), and register in the parent `creations/CMakeLists.txt`.
- **Dangling EntityIds across reset:** if a surviving entity holds a destroyed
  gameplay entity's id, it goes stale — keep cross-scene references out of
  surviving entities, or re-acquire after rebuild (#1814 CLAUDE.md).

## Sibling / in-flight reconciliation
- Parent #1814 (engine primitive) is MERGED — this is its sanctioned
  `[engine primitive] -> [demo]` carve-off, no overlap.
- No open PR touches `creations/demos/` scene-reset surface,
  `entity_manager` teardown, or `system_manager` pipeline registration
  (checked open engine PRs #1742/#1890/#1891/#1907) — no in-flight conflict.
- Downstream consumer is a Lua scene/state-machine module driving
  `IRWorld.resetGameplay()`; this demo is the engine-side proof and adds no Lua.
