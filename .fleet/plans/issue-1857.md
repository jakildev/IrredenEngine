# Plan: demo: scene_reset — render-stack idempotency proof for resetGameplay (VoxelPool + render-survival)

- **Issue:** #1857
- **Model:** opus
- **Date:** 2026-06-19
- **Decomposition:** one opus PR (a new demo + one small additive engine accessor + its unit test)
- **Blocked by:** #1814 — **MERGED** (PR #1853, 2026-06-15). Dependency satisfied; this is plannable/claimable.

## Verified current state (what exists, what's missing)

Grounded in the merged #1814 surface (`engine/entity`, `engine/system`, `engine/render`,
`engine/prefabs/irreden/voxel`):

**Reset primitive — all shipped and reachable from a C++ demo:**
- `IREntity::resetGameplay()` — `engine/entity/include/irreden/ir_entity.hpp:187`,
  impl `engine/entity/src/ir_entity.cpp:19`. Destroys every live entity except
  singletons (the singleton cache is the preserve registry, not cleared) and
  `C_Persistent`-tagged entities; component-backing entities survive. Eager,
  main-thread. Fires each component's `onDestroy()`.
- `C_Persistent{}` — `engine/prefabs/irreden/common/components/component_persistent.hpp`.
  Empty tag.
- **RenderManager stamps `C_Persistent{}` on all 5 render entities** — camera,
  `m_mainFramebuffer`, `m_mainCanvas`, `m_backgroundCanvas`, `m_guiCanvas`
  (`engine/render/src/render_manager.cpp:131-135`). So the camera + canvases +
  framebuffer survive `resetGameplay()` and the renderer keeps working. **The demo
  does not need to re-create or re-tag any render entity** — render-survival is a
  property of the shipped engine, the demo only has to *prove* it.
- `IRSystem::clearPipeline(event)` / `registerPipeline(event, list)` —
  `engine/system/include/irreden/ir_system.hpp:474` / `:428`. `clearPipeline` ==
  `registerPipeline(event, {})`. Scene swap = clear + re-register per event.
- Live-entity count: `IREntity::getLiveEntityCount()` —
  `engine/entity/include/irreden/ir_entity.hpp:39`. Returns `EntityId` (uint64).
- Active canvas: `IRRender::getActiveCanvasEntity()` —
  `engine/render/include/irreden/ir_render.hpp:143`. The canvas owns the
  `C_VoxelPool` (one pool per canvas entity).

**THE GAP (drives the one engine change in this task):**
- The VoxelPool exposes only `C_VoxelPool::getLiveVoxelCount()`
  (`engine/prefabs/irreden/voxel/components/component_voxel_pool.hpp:235`), which
  returns `m_voxelPoolIndex` — the **monotonic allocation frontier (high-water
  mark)**. `deallocateVoxels` (`:177-201`) pushes freed spans onto the free list
  (`m_freeVoxelSpans` + `m_freeSpanLookup`) but **never decrements
  `m_voxelPoolIndex`** (verified: the only write sites are `= 0` init and `+= size`
  on a frontier-bump allocation; no `-=` anywhere in the tree).
- Consequence: after `resetGameplay()` frees a scene's voxels, `getLiveVoxelCount()`
  does **not** return to baseline — it **plateaus** at the high-water mark (next
  scene reuses freed spans via `findFreeSpan`). With *differently-sized* scenes A/B
  (which the acceptance criteria require), free-list fragmentation can even nudge
  the frontier up while frees are perfectly correct → asserting on
  `getLiveVoxelCount()` would be both wrong-by-wording ("returns to baseline") and a
  false-positive leak detector.
- **There is no public free-list-aware "in-use" accessor today.**
  `m_freeVoxelSpans` / `m_freeSpanLookup` are private with no summing accessor.
  ⇒ The acceptance criterion "VoxelPool allocated-voxel count **returns to baseline**
  each cycle" is not expressible from the current public surface. This task adds the
  missing accessor (additive, ~5 lines) so the assertion is exact and
  scene-size-independent.

**Demo harness pattern (template = `creations/demos/chunk_streaming_smoke/`):**
- A headless `runXTest()` runs **before** `IREngine::gameLoop()`, exercises the real
  voxel pool directly (no frames needed — pool + entity-count state is
  frame-independent), logs `IR_LOG_INFO("[...] PASS: ...")` /
  `IR_LOG_ERROR("[...] FAIL: ...")`, and increments a `g_smokeFailCount`.
  `main` returns `g_smokeFailCount > 0 ? 1 : 0` (exit code is the fleet signal).
- `--auto-screenshot <N>` is parsed via `IRVideo::parseAutoScreenshotArgv`; the
  auto-screenshot system appended to the RENDER pipeline drives capture **and the
  clean exit** from `gameLoop()` (that's why `main` can return after `gameLoop()`).
- Demos register in **`creations/CMakeLists.txt`** (NOT `creations/demos/CMakeLists.txt`,
  which does not exist) via `add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/demos/<name>)`.
- `resetGameplay()` itself is already unit-covered headlessly in
  `test/ecs/entity_manager_test.cpp:268+` (singleton/persistent survival, live-count
  idempotency) — this task is the **render-stack + VoxelPool** half a unit test can't
  reach, exactly as #1814's plan (step 7) scoped it.

## Approach (one approach, picked)

Two pieces in one PR: (1) add the missing free-list-aware VoxelPool accessor +
test; (2) the new `scene_reset` demo that asserts idempotency headlessly and proves
render-survival via the post-loop screenshot.

### Piece 1 — `C_VoxelPool::getInUseVoxelCount()` (engine, additive)

In `engine/prefabs/irreden/voxel/components/component_voxel_pool.hpp`:
- Add a private CPU-only counter `int m_inUseVoxelCount = 0;` (alongside
  `m_voxelPoolIndex` at `:687`).
- In **both** successful return paths of `allocateVoxels` (the free-span-reuse path
  `:127-145` and the frontier-bump path `:148-163`): `m_inUseVoxelCount += size;`
- In `deallocateVoxels` (`:177-201`, after the bounds assert): `m_inUseVoxelCount -= size;`
- Add the accessor next to `getLiveVoxelCount` (`:235`):
  ```cpp
  // True count of voxels currently allocated (frontier minus freed spans).
  // Unlike getLiveVoxelCount() (the monotonic high-water frontier), this
  // returns to its pre-allocation baseline after every span is freed — the
  // metric a scene-reset idempotency test asserts on. #1857.
  int getInUseVoxelCount() const { return m_inUseVoxelCount; }
  ```
- **Why a running counter, not summing `m_freeSpanLookup` on demand:** the counter
  is O(1), unambiguous, and independent of the free-list's internal bucketing /
  reuse quirks (e.g. the size-keyed `m_freeSpanLookup` vs. append-only
  `m_freeVoxelSpans` divergence). It cannot drift because there are exactly three
  maintenance sites (2 alloc returns + 1 dealloc), all in this one component.
- **Do NOT remove or change `getLiveVoxelCount()`** — other code (chunk-bounds,
  `chunk_streaming_smoke`) reads the frontier; this is purely additive.

Test (extend an existing pool-aware test, do not add a new CMake target):
`test/ecs/voxel_set_target_canvas_test.cpp` already drives a canvas + pool (via its
own `m_entityManager` fixture, `:26`). Add a case: record `getInUseVoxelCount()`
baseline → construct N `C_VoxelSetNew` sets of *varying* sizes → assert in-use grew
by the sum → mark them for destruction → **drain the deferred deletions** (see below)
→ assert `getInUseVoxelCount()` returned to baseline, **including across a second
differently-sized round** (exercises free-span reuse). Confirm the file's existing
fixture exposes the active canvas's `C_VoxelPool`; if not, mirror how that test
reaches the pool.

**Deferred-deletion drain — don't skip, or the assertion is a silent false
negative:** entity destroy is *deferred*. `IREntity::destroyEntity` only calls
`markEntityForDeletion` (`ir_entity.cpp:11-13`), and the pool ranges are freed by
`C_VoxelSetNew::onDestroy`, which fires only when the marked list is processed — so
asserting right after the `destroyEntity` calls reads the pre-free count and a
*correct* reset looks like a leak. After marking the sets, call
`m_entityManager.destroyMarkedEntities()` **before** the baseline assertion (drive
the same manager the fixture uses; precede it with
`m_entityManager.flushStructuralChanges()` if the case also queued deferred
component ops). `flushStructuralChanges()` alone does **not** drain the
deletion-marked list — that is `destroyMarkedEntities()`'s job
(`entity_manager.cpp:204`). This mirrors what `resetGameplay()` →
`destroyAllExceptPreserved` runs internally (`flushStructuralChanges()` then
`destroyMarkedEntities()`, `entity_manager.cpp:378-379`) — which is exactly why the
**demo's** assertions need no explicit drain.

### Piece 2 — `creations/demos/scene_reset/` (new demo)

Files: `main.cpp`, `CMakeLists.txt`, `config.lua` (copy
`chunk_streaming_smoke/config.lua` verbatim). Register in `creations/CMakeLists.txt`.

`main.cpp` structure (mirror `chunk_streaming_smoke/main.cpp`):
1. `parseAutoScreenshotArgv` → `IREngine::init(argv[0])` → `initSystems()` →
   `initCommands()` → `runSceneResetTest()` → `initEntities()` (build the final,
   on-screen scene so the screenshot is non-empty) → `IREngine::gameLoop()` →
   `return g_sceneResetFailCount > 0 ? 1 : 0;`
2. `initSystems()` registers a minimal RENDER pipeline sufficient to draw voxels
   (camera control + `VOXEL_TO_TRIXEL_STAGE_1` + `TRIXEL_TO_FRAMEBUFFER` +
   `FRAMEBUFFER_TO_SCREEN`; plus `PROPAGATE_TRANSFORM` + `UPDATE_VOXEL_SET_CHILDREN`
   in UPDATE), and appends the auto-screenshot system when `g_autoWarmupFrames > 0`
   (copy the `cfg`/`kShots` block from chunk_streaming_smoke; one or two zoom shots
   of the final scene is enough).
3. `runSceneResetTest()` — the idempotency proof, headless (no frames; pool +
   entity-count state is frame-independent, same precedent as
   `runChunkSmokeTest()`):
   ```
   canvasEntity = IRRender::getActiveCanvasEntity();
   // getComponent in setup code (not a tick) is fine:
   pool& = IREntity::getComponent<C_VoxelPool>(canvasEntity);
   inUse0   = pool.getInUseVoxelCount();      // baseline = preserve set's voxels (≈0 gameplay)
   ents0    = IREntity::getLiveEntityCount(); // baseline = singletons + C_Persistent + component-backers
   for cycle in 1..N (N >= 10):
       buildScene(cycle even ? A : B);  // A and B DIFFER: entity count, set sizes, colors, positions
       assert(pool.getInUseVoxelCount() > inUse0 && IREntity::getLiveEntityCount() > ents0); // scene built
       // exercise the full documented scene-swap surface + ordering contract:
       IRSystem::clearPipeline(IRTime::Events::UPDATE);
       IREntity::resetGameplay();
       IRSystem::registerPipeline(IRTime::Events::UPDATE, {<next scene's systems>});
       assert(pool.getInUseVoxelCount() == inUse0);     // VoxelPool baseline — voxels freed
       assert(IREntity::getLiveEntityCount() == ents0); // entity baseline — no gameplay leak
       // re-reading the canvas pool every cycle also proves the canvas survived (render-stack survival)
       log PASS/FAIL (increment g_sceneResetFailCount on mismatch)
   log final aggregate PASS/FAIL.
   ```
   `buildScene` creates a handful of `C_VoxelSetNew` voxel-set entities (use the
   `ivec3` bounds + `Color` ctor, `true` for the dense fill — see
   `chunk_streaming_smoke/main.cpp:248-251`). A and B differ so the test exercises
   varying allocation footprints (the case the new accessor exists to handle).
4. `initEntities()` builds the final on-screen scene (one of A/B) so the
   `--auto-screenshot` frame captures a rendered scene **after** all N resets — the
   visual render-survival proof (camera + canvases carried `C_Persistent`).

## Affected files
- `engine/prefabs/irreden/voxel/components/component_voxel_pool.hpp` — add
  `m_inUseVoxelCount` member, maintain at the 2 alloc + 1 dealloc sites, add
  `getInUseVoxelCount()` accessor.
- `test/ecs/voxel_set_target_canvas_test.cpp` — add the in-use-count
  alloc/destroy/baseline round-trip case (varying sizes, two rounds).
- `creations/demos/scene_reset/main.cpp` (new) — demo + idempotency assertions +
  render-survival final scene.
- `creations/demos/scene_reset/CMakeLists.txt` (new) — mirror
  `chunk_streaming_smoke/CMakeLists.txt` (`add_executable(IRSceneReset main.cpp)`,
  link `IrredenEngine`, `irreden_bundle_assets(IRSceneReset SCRIPTS config.lua)`,
  `IRSceneResetRun` target).
- `creations/demos/scene_reset/config.lua` (new) — copy of
  `chunk_streaming_smoke/config.lua`.
- `creations/CMakeLists.txt` — add
  `add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/demos/scene_reset)` (append; do not
  reshuffle).

## Cross-system audit (the one shared-resource change)
Adding `m_inUseVoxelCount` + `getInUseVoxelCount()` to `C_VoxelPool` is **purely
additive**: no consumer of `C_VoxelPool` is modified, `getLiveVoxelCount()` is
untouched, and the new member is a **CPU-only** field on the CPU-side component — it
is **not** part of any GPU SSBO struct (the GPU voxel layout is `C_Voxel` / the
position/color spans, not the pool bookkeeping members), so there is no std430 /
binding-slot impact and no shader change. No migration of existing call sites is
required.

## Acceptance criteria
- `creations/demos/scene_reset/` builds clean on the host preset (`IRSceneReset`).
- Headless run (`--auto-screenshot N`) logs a per-cycle PASS and a final aggregate
  PASS across N≥10 cycles; exit code 0.
- Each cycle asserts, after `resetGameplay()`: live-entity count == baseline AND
  `C_VoxelPool::getInUseVoxelCount()` == baseline (with differently-sized scenes A/B).
- The captured screenshot shows a rendered scene (camera + canvases survived all
  resets).
- `getInUseVoxelCount()` round-trip unit test passes
  (`test/ecs/voxel_set_target_canvas_test.cpp`).
- Builds + runs headless on the host preset (macOS/Metal here; the demo is
  backend-agnostic — Linux/GL parity is a smoke follow-up, not in this PR).

## Gotchas
- **Do not assert on `getLiveVoxelCount()` for the baseline check** — it is the
  monotonic frontier and will NOT return to baseline (it plateaus). Use the new
  `getInUseVoxelCount()`. This is the entire reason Piece 1 exists; a worker who
  skips the accessor and reuses `getLiveVoxelCount()` will ship a test that passes
  for the wrong reason (or false-fails on fragmentation).
- **`getComponent<C_VoxelPool>` is only OK in demo setup code, never a tick.** The
  test loop runs in `runSceneResetTest()` (pre-`gameLoop`, no system iterating), so
  it's fine — do not move the count reads into a system tick.
- **Ordering contract (#1814):** within one logical step do
  `clearPipeline` → `resetGameplay()` → `registerPipeline` → spawn next scene, so no
  system would ever tick against destroyed entities. The headless loop has no ticking
  systems, but follow the order anyway to model the contract the demo documents.
- **Render-survival is proven two ways, both already covered:** (a) re-reading the
  canvas's `C_VoxelPool` every cycle would throw/crash if the canvas had been
  destroyed — so a green loop already proves canvas survival; (b) the post-loop
  `--auto-screenshot` frame is the visual confirmation. No single-frame-step API is
  exposed (`ir_engine.hpp` only offers `init` + blocking `gameLoop`), so do NOT try
  to render between cycles — that is why the count-idempotency is proven headless,
  exactly like `chunk_streaming_smoke`.
- **Demo registration file is `creations/CMakeLists.txt`** (there is no
  `creations/demos/CMakeLists.txt`).
- **`C_VoxelSetNew` allocates on construction** and asserts if there's no active
  canvas — the canvas exists after `IREngine::init`, so building scenes inside
  `runSceneResetTest()` (post-init) is safe. Check `numVoxels_ > 0` if you want a
  defensive guard.
- Keep the demo **small and deterministic** (no RNG) per `creations/demos/CLAUDE.md`
  — A and B are fixed layouts.
