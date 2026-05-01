# TASKS

Shared task queue for parallel agents. Both human and agent maintainers
append here, and the next unblocked item is what an idle agent should pick up.

## How to use this file

1. **Picking a task:** skim the `## Open` section. Find the first `[ ]` item
   whose **Owner** is `free` or your worktree name, and whose **Blocked by**
   list is empty. **Then cross-check `gh pr list --state open`** — if any
   open PR's title or branch name looks like it's already working on that
   task, skip to the next candidate. The open-PR list is the authoritative
   claim signal; the `[~]` flip on a feature branch is invisible to other
   agents until merge, so two agents can race to claim the same task in the
   ~minutes-to-hours window between picking and merging. Cross-checking
   `gh pr list` closes most of that race.

   Once you've picked, change the status to `[~]` (in progress), set Owner
   to your worktree, and push the edit in your first commit so other agents
   see it once your PR merges.
2. **Finishing a task:** change `[~]` to `[x]`, set the final commit or PR
   URL in the **Links** line, and move the item to `## Done — last 20` at
   the bottom. Keep only the last 20 done items; prune older ones.
3. **Adding a task:** append to `## Open` with the template below. Err on the
   side of creating small tasks (one PR's worth of work). If a task needs
   research first, file it as `Research:` — the deliverable is a short
   findings note, not code. The fastest way to add a task is to ask the
   `queue-manager` pane in the fleet — paste a rough description and it
   will categorize, tag, format, and file the queue-update PR for you.
4. **Blocking on another task:** put the blocking task's title in
   **Blocked by**. An agent should skip blocked items. For cross-repo
   blocks (game blocked on engine), put the engine PR URL in **Blocked by**
   so any agent can resolve it without context.
5. **Touching this file:** always stage and commit `TASKS.md` edits in the
   same PR as the work they describe, so history stays consistent.
   Queue-maintenance-only PRs (e.g. `queue: add task X`, batched task
   adds) are also explicitly allowed and merge fast.

### Race conditions and how the fleet handles them

`TASKS.md` is git-versioned, which means an agent's `[~]` claim only
becomes visible to other agents after its PR merges. Between picking and
merging, two agents can independently pick the same task. The fleet
defends against this in three layers:

1. **Pre-pick `gh pr list` cross-check** (rule 1 above) — closes most
   of the window.
2. **Merge conflict on the second `[~]` flip** — both PRs edit the same
   line in `TASKS.md`, so whichever one merges second will hit a
   GitHub-side merge conflict and refuse to auto-merge. The human
   reviewer sees the conflict before merging and rejects the loser.
3. **Loser requeues and picks again** — the agent whose PR conflicts
   uses `start-next-task` to reset to a fresh branch off `origin/master`,
   picks the next available task, and moves on. The work isn't lost; it
   just gets rescheduled.

The local `fleet-claim` script adds a fourth layer: agents call
`fleet-claim claim "T-NNN" <agent>` using the task's **ID** (not the
free-text title) before starting work. The short deterministic ID
prevents the failure mode where two agents slugify different
paraphrasings of the same title and both succeed. If `fleet-claim`
returns exit 1 (already taken), skip to the next task.

This file is the **engine-level** task queue. Private creations that live
under `creations/` may define their own `TASKS.md` inside their own
directory — those are tracked independently and should not be mixed here.
Do not queue game or creation-specific gameplay tasks in this file; queue
them in the creation's own `TASKS.md`.

## Task template

```
- [ ] **<short title>** — <one-line goal>
  - **ID:** T-NNN  (sequential, assigned by the queue-manager)
  - **Area:** engine/render | engine/entity | engine/prefabs/... | docs | build | creations/demos/... | ...
  - **Model:** opus | sonnet  (which model should run this)
  - **Owner:** free | <worktree-name>
  - **Blocked by:** (none) | <title of blocking task>
  - **Stack:** T-XXX..T-YYY <slug>  (optional — only for tasks in a stacked chain sharing a parent epic; omit for standalone tasks)
  - **Acceptance:** <concrete check: build passes, test X passes, PR merged, screenshot Y looks like Z>
  - **Issue:** (none) | #N  (GitHub issue number, if task originated from an issue)
  - **Notes:** <context, links, prior attempts>
  - **Links:** (fill in PR URL when done)
```

The **ID** is the canonical claim key. When calling `fleet-claim`, pass the
task ID (e.g. `fleet-claim claim "T-003" sonnet-fleet-1`), **not** the
free-text title. IDs are short and unambiguous — agents can't accidentally
paraphrase them, which is the failure mode that free-text title slugification
is vulnerable to.

The **Stack** field groups child tasks of a shared parent epic so a
human can follow the chain across `## Open`. Format:
`T-<min>..T-<max> <slug>`; slug is a kebab-case identifier shared by
all siblings. Informational only — `fleet-claim` and the scout cache
ignore it. Standalone tasks omit the field entirely. The queue-manager
populates it during ingestion when a child issue declares membership;
see `role-queue-manager.md` for the detection rule.

Status markers: `[ ]` open, `[~]` in progress, `[x]` done, `[!]` blocked/stuck.

### Model tagging (important)

Tag every task with the intended model. Default assumption:

- `[opus]` — anything touching `engine/render/`, `engine/entity/`,
  `engine/system/`, `engine/world/`, `engine/audio/`, `engine/video/`,
  `engine/math/` (non-trivial), or ECS/render optimization, or concurrency,
  or ownership/lifetime rules. Also final review on anything important.
- `[sonnet]` — test generation, doc passes, mechanical refactors with a
  clear spec, first-pass code review, clearly-scoped items already thought
  through, anything in `creations/demos/`, small bounded shader tweaks.

A Sonnet agent that picks up an `[opus]` task should escalate instead of
charging ahead. A Sonnet agent that finds a `[sonnet]` task is subtler
than expected (touches an invariant, a lifetime, a race) should stop and
requeue with `[opus]`. The top-level `CLAUDE.md` has the full split.

## Good tasks to queue here (engine-only)

Small and bounded is the target. Good shapes for this queue:

- **Test generation** — "write exhaustive tests for `engine/math/physics.hpp`
  ballistic helpers"
- **Docs / API reference** — "document every `IRRender::` free function in
  `engine/render/CLAUDE.md`"
- **Benchmark / profiling report** — "profile `IRShapeDebug` at zoom 4 with
  N voxels and file a report"
- **Isolated refactor** — "port `engine/common/ir_constants.hpp` to constexpr"
- **Build / CI hardening** — "add a `format-check` CI target that fails on
  stale clang-format output"
- **FFmpeg / audio interface hardening** — "add bounds checks to
  `VideoRecorder::submitVideoFrame` stride handling"
- **Compile-time cleanup** — "reduce `engine/render/` TU rebuild cascade by
  moving X out of the low header"
- **Shader hygiene** — "extract repeated iso-projection math in
  `engine/render/src/shaders/` into `ir_iso_common.glsl`"

Avoid:

- Tasks that touch core ECS types (`engine/entity/`) — do those by hand.
- "Refactor the render loop" — too broad, no single PR scope.
- Anything that would require changing the public `ir_*.hpp` surface across
  multiple modules in one PR.
- Gameplay or content work for any specific creation — belongs in that
  creation's own task queue.

---

## Open

<!-- Add tasks below this line. -->


- [~] **Render: trixel rasterization under cardinal-snap Z-yaw** — update trixel raster shader to pick one of 4 basis-vector permutations from `rasterYaw`; GLSL + MSL parity
  - **ID:** T-055
  - **Area:** engine/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** claude/T-055-trixel-cardinal-yaw
  - **Blocked by:** (none)
  - **Stack:** T-054..T-058 z-yaw-pipeline
  - **Acceptance:** (1) shader selects the correct basis-vector permutation for each cardinal yaw (0°, 90°, 180°, 270°); (2) `render-debug-loop` visual parity at yaw=0; (3) correct rasterization at all four cardinal angles verified visually; (4) per-frame cost at any cardinal yaw ≤ cost at yaw=0 (no regression); (5) GLSL and MSL implementations at parity; (6) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`
  - **Issue:** #311
  - **Notes:** Child 2 of 5 of epic #310. Re-scoped from "arbitrary" to "cardinal-snap" — continuous visual rotation comes from the residual 2D composite pass (new unfiled sibling). `rasterYaw` provided by T-054. Does NOT handle fractional/sub-cardinal yaw — voxels always land on integer trixel pixels. Full plan: `.fleet/plans/T-054.md`.
  - **Links:**

- [~] **Render: SDF shape rasterization under arbitrary Z-yaw** — update SDF compute shader to consume continuous `visualYaw`; GLSL + MSL parity
  - **ID:** T-056
  - **Area:** engine/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** claude/T-056-sdf-yaw
  - **Blocked by:** (none)
  - **Stack:** T-054..T-058 z-yaw-pipeline
  - **Acceptance:** (1) SDF shader reads `visualYaw` (continuous radians, provided by T-054); (2) `render-debug-loop` visual parity at yaw=0; (3) correct SDF rendering at several non-cardinal yaw values verified visually; (4) GLSL and MSL implementations at parity; (5) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`
  - **Issue:** #312
  - **Notes:** Child 3 of 5 of epic #310. SDF is the "free half" — no integer-alignment requirement, so continuous-yaw works directly. Reads `visualYaw` (not `rasterYaw`). Runs in parallel with T-055 once T-054 lands. Full plan: `.fleet/plans/T-054.md`.
  - **Links:**

- [ ] **Render/input: screen-to-world picking under Z-yaw** — update picking inverse to compose `R2D(-residualYaw)` then `R(-rasterYaw)·M⁻¹`; audit duplicate transform copies
  - **ID:** T-057
  - **Area:** engine/render, engine/input
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-058
  - **Stack:** T-054..T-058 z-yaw-pipeline
  - **Acceptance:** (1) picking inverse composes `R2D(-residualYaw)` then `R(-rasterYaw)·M⁻¹` per plan; (2) correct world coords at yaw=0 (no regression for any existing consumer); (3) correct world coords at ≥4 non-cardinal yaw values; (4) audit of duplicate screen↔world transform copies in `engine/render/` and input-side consumers complete; (5) `fleet-build --target IRShapeDebug` clean
  - **Issue:** #313
  - **Notes:** Child 5 of 5 of epic #310. Sequenced last — inverts the full composition once both T-055 (cardinal raster) and the residual composite pass (T-058) land. Full plan: `.fleet/plans/T-054.md`.
  - **Links:**

- [ ] **Render: screen-space 2D residual yaw composite pass** — add `SCREEN_SPACE_RESIDUAL_ROTATE` pipeline stage that rotates the trixel canvas by `residualYaw`; GLSL + MSL parity
  - **ID:** T-058
  - **Area:** engine/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-055
  - **Stack:** T-054..T-058 z-yaw-pipeline
  - **Acceptance:** (1) new GLSL + MSL shader pair committed under `engine/render/src/shaders/`; (2) new system registered at `SCREEN_SPACE_RESIDUAL_ROTATE` pipeline slot; (3) at `visualYaw=0°` the framebuffer is pixel-identical to canvas content (passthrough); (4) at `visualYaw=30°` with cardinal-snap raster from T-055, visible result rotates 30° in screen space without distortion beyond expected bilinear filtering; (5) `render-debug-loop` screenshots showing rotations at 0°, 30°, 60°, 90°, 120° demonstrating smooth continuity; (6) builds clean on `linux-debug` AND `macos-debug`
  - **Issue:** #322
  - **Notes:** Child of epic #310 (z-yaw-pipeline). Slots between `TRIXEL_TO_TRIXEL` and `FRAMEBUFFER_TO_SCREEN` in the render pipeline. Reads `residualYaw` from T-054. Uses bilinear filtering; pivot is canvas center. At `residualYaw=0`, must be pixel-identical (add explicit epsilon early-out if needed). Blocks T-057 (picking must invert this pass AND the cardinal raster). Full plan: `.fleet/plans/T-054.md`.
  - **Links:**

- [~] **Render systems: migrate 12 files off function-local static onto SystemParams** — replace all mutable function-local `static` state in 12 render system headers with `SystemParams` structs; visual and performance identity required
  - **ID:** T-065
  - **Area:** engine/prefabs/irreden/render/systems
  - **Model:** opus
  - **Owner:** claude/T-065-render-system-params
  - **Blocked by:** (none)
  - **Acceptance:** (1) all 12 listed files use `SystemParams` for mutable system state; (2) grep shows no function-local `static` for mutable state in the 12 files — all surviving `static` matches are `constexpr` constants or program-lifetime resource pointers (each called out in PR body); (3) `render-debug-loop` before/after screenshots pixel-identical — attach via `attach-screenshots` skill; (4) `IRShapeDebug` pipeline tick cost at zoom 4 / 1000+ voxels within ±2% of pre-migration baseline (measured, reported in PR body); (5) builds clean on `linux-debug` AND `macos-debug`; (6) OpenGL and Metal backends render identically
  - **Issue:** #344
  - **Notes:** Soft dep on T-064 landing first (so canonical pattern is documented), but not a hard block — canonical pattern is in the issue body. One PR for all 12 files per architect direction. `thread_local` scratch buffer in `system_shapes_to_trixel.hpp::buildAndUploadTileDescriptors` is exempt per architect recommendation (leave as `thread_local` with explanatory comment). Resource pointers fetched at create time may stay `static` or move into `Params` per worker judgment — document in PR body. Behavior preservation is the hard gate — stop and escalate on ANY behavioral difference. Some statics may be relied-upon bugs; flag in PR body.
  - **Links:**

- [ ] **Render/system: centralize GPU stage probes via SystemManager TickObserver** — add a generic `TickObserver` hook to `SystemManager`, implement `GpuStageTimingObserver` in render layer, and remove the 15+ inlined probe blocks from render system headers
  - **ID:** T-066
  - **Area:** engine/system, engine/render, engine/prefabs/irreden/render
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `IRSystem::TickObserver` + `registerTickObserver`/`unregisterTickObserver` in `ir_system.hpp`/`system_manager.hpp`, fire pre/post in `SystemManager::executeSystem`; empty-observer-list path has no measurable overhead; (2) `GpuStageTimingObserver` + `IRRender::tagGpuStage` exist; observer installed once during render init; (3) all inlined GPU probe blocks under `engine/prefabs/irreden/render/systems/` removed; (4) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`; Lua `ir.render.getPassTimings()`/`getPassTiming()` returns same 15+ rows with same field semantics; (5) a creation with >1 matching canvas for a formerly-`=` system reports sum across entities, not the last one
  - **Issue:** #261
  - **Notes:** T-028 follow-up. The 3-line inline probe pattern is duplicated 15+ times (commenter notes more systems may have been added since the issue was filed). Fast-path rule: `m_observers.empty()` check compiles down to single branch. Follow the `optimize` skill before `commit-and-push`. Non-goals: real async GPU timer queries (Part 2, separate task); changing Lua API surface.
  - **Links:**

- [ ] **Metal: sync FrameDataVoxelToTrixel struct and C++ feeder with GLSL yaw fields** — add `visualYaw`, `rasterYaw`, `residualYaw`, `_yawPadding` to the Metal struct in `ir_iso_common.metal` and to the C++ `FrameDataVoxelToCanvas` struct in `ir_render_types.hpp`
  - **ID:** T-067
  - **Area:** engine/render, shaders/metal
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) four yaw fields added to Metal `FrameDataVoxelToTrixel` in `ir_iso_common.metal` matching std140 layout; (2) four fields added to C++ `FrameDataVoxelToCanvas` in `ir_render_types.hpp`; populated each frame from camera-state component; (3) `c_voxel_visibility_compact.metal`, `c_voxel_to_trixel_stage_1.metal`, `c_voxel_to_trixel_stage_2.metal`, `c_lighting_to_trixel.metal` all compile clean; (4) `fleet-build --target IRShapeDebug` (or any voxel-pipeline creation) clean on `macos-debug`; GLSL pipeline still renders correctly with initialized yaw values
  - **Issue:** #337
  - **Notes:** Metal shaders crash at runtime referencing `frameData.rasterYaw` which doesn't exist in the Metal struct. The C++ struct is also missing the four fields (GLSL has been reading garbage/zero-padding). Default value when no rotation is happening is 0 for all three yaws — forward-compatible. macOS/Metal only crash; WSL/Linux (OpenGL) is unaffected.
  - **Links:**

- [ ] **Render/shader: SDF fast-path redesign under non-zero Z-yaw + snap-mode/voxel-pool alignment** — restructure SDF dispatch to eliminate confusing nesting, add analytical fast paths at non-zero yaw, and align snap-mode voxel-pool carving with rasterYaw from T-055
  - **ID:** T-068
  - **Area:** engine/render/src/shaders, engine/prefabs/irreden/render/systems
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-055, T-056
  - **Acceptance:** (1) SDF dispatch path has analytical fast paths at non-zero yaw OR the O(N) brute-force path is explicitly documented as intentional with the confusing multi-branch structure removed; (2) `smoothMode` gate restructured to eliminate redundancy; (3) snap-mode voxel-pool carving uses `rasterYaw` to align with cardinal-snap trixel raster from T-055; (4) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`; (5) `render-debug-loop` screenshots at yaw=0 (byte-identical to master), yaw=π/6, yaw=π/4, yaw=π/2 verify no coverage gaps
  - **Issue:** #345
  - **Notes:** Escalated from sonnet-author after human:needs-fix on PR #334 (T-056). Three human concerns: (1) confusing nesting + redundant smoothMode check, (2) no analytical fast path at non-zero yaw (existing `boxDepthIntersect` etc. bake in yaw=0 iso direction), (3) voxel-pool carving not rotating with T-055. Blocked until both T-055 (cardinal-snap raster) and T-056 (SDF arbitrary yaw) land. Opus architectural decision required on whether to rederive analytical intersectors for rotated ray.
  - **Links:**

- [ ] **Metal: port entity-id readback into f_trixel_to_framebuffer** — add `HoveredEntityIdBuffer` SSBO, `triangleEntityIds` texture, and hover-detection logic to `trixel_to_framebuffer.metal` fragment shader to match GLSL parity; restores Mac hover/click
  - **ID:** T-069
  - **Area:** engine/render, shaders/metal
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) Metal `FrameDataIsoTriangles` extended with `int distanceOffset` matching GLSL + C++ `FrameDataTrixelToFramebuffer`; (2) `trixel_to_framebuffer.metal` fragment receives `texture2d<uint> triangleEntityIds [[texture(2)]]` and `device HoveredEntityIdBuffer& hovered [[buffer(14)]]`; (3) hover/click logic ported from `f_trixel_to_framebuffer.glsl` (origin parity, entity ID sampling, depth test, hover highlight); (4) `fleet-build` clean on `macos-debug`; `IRShapeDebug` and any hover-enabled demo shows `[HoverDetect]` logs on hover; `IRInput.onEntityHovered`/`onEntityClicked` Lua callbacks fire correctly on Mac
  - **Issue:** #353
  - **Notes:** Root cause: `f_trixel_to_framebuffer.glsl` writes `HoveredEntityIdBuffer` with parity/origin/depth logic; the Metal counterpart `trixel_to_framebuffer.metal` only samples colors + distances with no SSBO/entity-id logic. CPU buffer and C++ binding (slot 14) already exist in `system_trixel_to_framebuffer.hpp`. `C_TriangleCanvasTextures::bind(0,1,2)` already binds entity IDs to texture unit 2. Metal runtime binds SSBOs by slot via `setBuffer` in `metal_render_impl.cpp`.
  - **Links:**

- [ ] **Render: screen-space sun shadow map — add bake pass (flag-guarded)** — implement `system_bake_sun_shadow_map` + GLSL/Metal compute shaders that project rasterized iso pixels into a sun-aligned 2D buffer via `imageAtomicMin`; gate behind `useScreenSpaceShadow_` flag
  - **ID:** T-070
  - **Area:** engine/render, engine/prefabs/irreden/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) new `system_bake_sun_shadow_map.hpp` + GLSL + Metal compute shaders committed; (2) `c_compute_sun_shadow.glsl` and Metal counterpart branch on `useScreenSpaceShadow_` flag; both paths produce equivalent shadow silhouettes; (3) `render-debug-loop` comparison screenshots show side-by-side parity; (4) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`; (5) shadow silhouettes within ≤1 sun-space texel of existing implementation when flag disabled; (6) per-pixel cost drops from O(canvasPixels×64) to O(canvasPixels) when flag enabled
  - **Issue:** #358
  - **Notes:** PR 1 of 2 from issue #358. Full design at `docs/design/screen-space-sun-shadow-map.md`. Bake pass projects `trixelDistances` pixels into sun-aligned 2D buffer via `imageAtomicMin` on packed depth; lookup pass reads one texel per pixel. PR 1 adds new system alongside existing paths — no deletion yet. PR 2 (T-071) deletes occupancy grid + analytic caster paths; blocked on T-065 landing first.
  - **Links:**

- [ ] **Render: screen-space sun shadow map — delete occupancy grid + analytic caster paths** — remove `BUILD_OCCUPANCY_GRID`, `C_OccupancyGrid`, `SunShadowShapeCasterBuffer`, `analyticShapeShadowHit`, and in-shader SDF helpers after T-070 establishes the screen-space path
  - **ID:** T-071
  - **Area:** engine/render, engine/prefabs/irreden/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-065, T-070
  - **Acceptance:** (1) `BUILD_OCCUPANCY_GRID`, `C_OccupancyGrid`, `SunShadowShapeCasterBuffer`, `analyticShapeShadowHit`, and in-shader SDF helpers removed; (2) `system_bake_sun_shadow_map` is the sole shadow producer; (3) `engine/render/CLAUDE.md` and `engine/prefabs/irreden/render/CLAUDE.md` updated to drop occupancy/analytic-caster sections; (4) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`; shadow renders correctly; (5) revisit `C_CanvasAOTexture`/`C_CanvasSunShadow` construction per #367 during deletion pass
  - **Issue:** #358
  - **Notes:** PR 2 of 2 from issue #358. Must wait for T-065 (12-file render-system-params migration) to land first — `system_compute_sun_shadow.hpp` and `system_build_occupancy_grid.hpp` are the exact files T-065 migrates; if PR 2 races T-065, both sides conflict on every line. PR 1 (T-070) adds new path; this PR deletes the old one. Several closed issues (pre-existing size mismatch, multi-canvas SSBO collision, SDF shadow artifacts) resolved by construction once old paths are removed.
  - **Links:**

- [ ] **Render: GPU-side light-volume propagation (jump flooding / iterative dilation)** — replace CPU BFS + 8 MB subImage3D upload in `system_compute_light_volume.hpp` with GPU seed pass + jump-flood propagate pass(es)
  - **ID:** T-072
  - **Area:** engine/prefabs/irreden/render/systems, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-071
  - **Acceptance:** (1) `system_compute_light_volume.hpp` has no per-frame CPU `vector<>` or `std::fill` of volume-sized buffers; `subImage3D` upload for the volume texture removed; (2) GPU seed pass + propagate pass(es) replace CPU BFS; light sources seeded via compute dispatch; (3) `IRLightingCombined` and `IRLightingEmissive` outputs within sample-noise of pre-migration reference; (4) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`
  - **Issue:** #359
  - **Notes:** Blocked until T-071 lands — both this issue and #358 edit `c_lighting_to_trixel.glsl` sample path and adjacent light-volume systems. GPU LOS rules in `detail::hasLineOfSight` also need GPU port. Jump flooding: seed pass writes emissive RGB at world position; propagate pass(es) dilate light into adjacent voxels per LOS rules. Camera-anchored grid follow-up deferred. Eliminates per-light O(radius³) CPU BFS and ~8 MB upload per frame.
  - **Links:**

- [~] **ECS: support non-default-constructible component types in EntityManager::setComponent** — change `EntityManager::setComponent` to accept non-default-constructible types via in-place placement-new or move-construct; then restore deleted default ctors on `C_CanvasAOTexture` and `C_CanvasSunShadow`
  - **ID:** T-073
  - **Area:** engine/entity
  - **Model:** opus
  - **Owner:** claude/T-073-non-default-component
  - **Blocked by:** (none)
  - **Acceptance:** (1) `EntityManager::setComponent` (and related archetype-storage code) accepts non-default-constructible types via placement-new or move-construct overload; (2) `C_CanvasAOTexture() = delete;` and `C_CanvasSunShadow() = delete;` restored; existing creation code that passes size at construction compiles; (3) a test that tries to default-construct either component fails at compile time; (4) `fleet-build --target IrredenEngineTest` clean; all existing tests pass
  - **Issue:** #367
  - **Notes:** Lighting-fidelity-polish PR tried deleting the default ctors but `EntityManager::setComponent` requires default-constructibility internally (default-construct + assign). The default ctors were reverted to `= default;` with explanatory comment. Right fix: route `setComponent` through placement-new or caller-provided value to avoid default-construct requirement. This is an ECS storage refactor — not purely a render change. Natural synergy with T-071 (which deletes canvas-component adjacents).
  - **Links:**

- [~] **Fleet docs: add silent-correctness rule coverage to review-pr + simplify (Tier 1)** — add five missing engine-invariant checks (lighting culling, position-component split, beginTick/endTick signature, component-method tiers, std140 UBO sync) to the review-pr and simplify skill files
  - **ID:** T-074
  - **Area:** tooling, docs
  - **Model:** sonnet
  - **Owner:** claude/T-074-review-simplify-checks
  - **Blocked by:** (none)
  - **Acceptance:** (1) `review-pr/SKILL.md` has lighting-culling subsection (4 invariants from `engine/render/CLAUDE.md:218-294`); (2) render checklist includes ❌ `C_Position3D` for visual placement; (3) ECS checklist includes `beginTick`/`endTick` signature + empty-set guard items; (4) ECS checklist includes tier-c component-method violation item; (5) shader section extended with std140 rules + bind-point match check; (6) `simplify/SKILL.md` mirrors same checks for same file trigger patterns; (7) doc-only; no build required
  - **Issue:** #379
  - **Notes:** Tier 1 of 3 from issue #379. Five findings (#1–5) are additive checklist items — all text changes, no logic. Engine cites: lighting invariants at `engine/render/CLAUDE.md:218-294`; position split at `engine/CLAUDE.md:56-62` + `engine/entity/CLAUDE.md:65-69`; beginTick/endTick at `engine/system/CLAUDE.md:48-50,84-87`; component tiers at `engine/prefabs/CLAUDE.md:62-117`; std140 at `engine/render/CLAUDE.md:321-325`.
  - **Links:**

- [ ] **Fleet docs: calibrate Opus-only review checklist + process gaps (Tier 2)** — add "Opus-only items" section to review-pr, wire render-debug-loop into worker render-PR flow, rewrite blocker/needs-fix boundary, add GLSL-without-Metal parity flag
  - **ID:** T-075
  - **Area:** tooling, docs
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `review-pr/SKILL.md` has explicit "Opus-only items" section (GPU buffer lifetime, archetype-mutation race, allocator behavior, hot-path register pressure); (2) `role-opus-reviewer.md` clarifies Opus does not re-run Sonnet's checks; (3) `review-pr` render checklist has ❌ new `*.glsl` without matching `*.metal`; (4) `review-pr/SKILL.md` blocker-vs-needs-fix definition rewritten with concrete criteria; (5) `role-sonnet-author.md` and `role-opus-worker.md` invoke `render-debug-loop` for render-touching PRs after `attach-screenshots`
  - **Issue:** #379
  - **Notes:** Tier 2 of 3 from issue #379 (findings #6–9). Issue author tags as `[opus]` — especially #6 (Opus-only sub-checklist is a design call about what Opus should be uniquely doing). Finding #9 (`render-debug-loop` not gated) is referenced in `engine/render/CLAUDE.md:160-167`.
  - **Links:**

- [~] **Fleet docs: worker-doc process tweaks and tooling cleanup (Tier 3)** — fix fleet-run timeout floor, add SystemName pre-flight build target rule, make Opus escalation symmetric, fix Co-Authored-By trailer, add stacked-PR downstream interface note, add manager-pointer lifetime flag
  - **ID:** T-076
  - **Area:** tooling, docs
  - **Model:** sonnet
  - **Owner:** claude/T-076-worker-doc-tweaks
  - **Blocked by:** (none)
  - **Acceptance:** (1) worker docs updated: build target is engine lib or `IrredenEngineTest` when diff adds `engine/prefabs/**/systems/` files; (2) `fleet-run` timeout raised to ≥15s for non-screenshot demos; `--auto-screenshot` replaces `--timeout` for screenshot-capable demos; (3) Opus escalation in `role-opus-worker.md` symmetric: file issue, comment, release claim, exit (or `fleet:opus-blocked` label defined); (4) `commit-and-push/SKILL.md` Co-Authored-By parameterized or uses generic `Claude <noreply@anthropic.com>`; (5) `review-pr/SKILL.md` stacked-PR section notes upstream interface stability requirement; (6) `review-pr` ownership section flags stored `g_*Manager` pointers in objects that can outlive `World`
  - **Issue:** #379
  - **Notes:** Tier 3 of 3 from issue #379 (findings #10–15). Grab bag of small worker-doc tweaks. Finding #12 (Opus escalation) has two options: (a) symmetric file+comment+exit, or (b) `fleet:opus-blocked` label — pick (a) for symmetry with design-escalation flow.
  - **Links:**

- [~] **Fleet: label discipline — verdict-without-label, has-nits stripping, changes-made handoff** — collapse post-verdict and set-verdict-label into one step in review-pr skill; fix commit-and-push to add fleet:changes-made when removing fleet:needs-fix; scope has-nits stripping authority; add re-apply guard to reviewers
  - **ID:** T-077
  - **Area:** tooling, docs
  - **Model:** sonnet
  - **Owner:** claude/T-077-label-discipline
  - **Blocked by:** (none)
  - **Acceptance:** (1) `review-pr/SKILL.md` makes post-verdict + set-verdict-label indivisible (not a separate advisory step); (2) `commit-and-push/SKILL.md` atomically adds `fleet:changes-made` when removing `fleet:needs-fix` after a fix push; (3) identify which agent strips `fleet:has-nits` and scope its removal authority to leave it intact when `fleet:approved` is also co-present; (4) reviewer role docs add: before re-applying a verdict label, check for any commit/comment after the most recent reviewer's `submittedAt`; (5) doc-only; no build required
  - **Issue:** #384
  - **Notes:** Four anti-patterns from feedback channel 2026-04-26 to 2026-04-30: A1 reviewer posts verdict without setting label (PRs go invisible); A2 `fleet:has-nits` stripped within 1 second of being set (likely merger or label-cleanup task); A3 `commit-and-push` removes `fleet:needs-fix` without adding `fleet:changes-made`; A4 reviewer re-applies verdict label that author's fix-push had correctly cleared. Distinct from #379 (engine-style checklist coverage).
  - **Links:**

- [ ] **Fleet: worktree contention — extend branch-lock filter, abort merger rebase on give-up, prevent parent-clone misroute** — fix merger give-up path to abort + switch back; lift branch-lock filter into shared helper for merger + opus-worker; add Edit/Write hook to catch worktree misroute
  - **ID:** T-078
  - **Area:** tooling, docs
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) merger give-up path always runs `git rebase --abort && git switch claude/merger-scratch` before exiting; (2) shared `fleet-worktree-busy-branches` helper (or equivalent) applied at merger step 3.5 and opus-worker step 1c to skip branch-locked candidates; (3) a `settings.json PreToolUse:Edit|Write` hook warns or blocks when a worktree agent writes to the parent-clone path outside its worktree prefix (or role docs updated with explicit path-prefix rule); (4) doc and script changes only; no engine build required
  - **Issue:** #385
  - **Notes:** Three issues from feedback: B1 merger left mid-rebase state on `claude/T-055-trixel-cardinal-yaw` blocking opus-worker from checking out the branch; B2 `git checkout -B` fails with "branch is already used by worktree" in merger candidate selection and author pickup paths; B3 agent in worktrees/opus-worker-1 used absolute paths pointing to parent clone instead of own worktree — Edit succeeded silently, build passed against clean code. Blast radius of B3: if parent clone were on another branch, `git restore` could have nuked another agent's work.
  - **Links:**

- [ ] **Fleet: permissions and summaries-on-exit — .claude/commands/ writes, rm allowlist, restore non-architect summaries** — carve Edit/Write permission for .claude/commands/ files in open PR diff; allowlist rm for .fleet/plans/; restore fleet-down session summaries to all roles
  - **ID:** T-079
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) harness gate exception carved for `.claude/commands/*.md` files in an open PR's diff (or inline-Python workaround officially documented in role docs); (2) `~/.claude/settings.json` allowlist includes `rm -f ~/.fleet/plans/*.md` and `rm -f .fleet/plans/*.md`; (3) `fleet-down` session summaries write for all active roles (workers, reviewers, queue-manager, merger), not just architects; smoke-test added to fleet-down asserting summary file count matches active-role count
  - **Issue:** #386
  - **Notes:** Three issues: C1 `.claude/commands/` Edit blocked even for files in an open PR's diff — inline-Python workaround discovered (`python3 -c "with open(path, 'w') as f: f.write(content)"`); C2 `rm -f ~/.fleet/plans/*.md` blocked by Bash sandbox — plan files accumulate forever; D1 `fleet-down` session summaries only written by architect roles since ~2026-04-20 — workers/reviewers/queue-manager/merger no longer writing summaries.
  - **Links:**

- [ ] **Fleet: orchestration calibration — babysit cooldown, fleet-claim git-aware, stale-status auto-flip** — enforce 30m floor between opus-reviewer relaunches; make fleet-claim resolve blockers via git merge-base; queue-manager detects stale [~] tasks merged to master and auto-flips to [x]
  - **ID:** T-080
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) fleet-babysit enforces ~30m floor between opus-reviewer relaunches OR opus-reviewer does live `gh pr view <N> --json reviews` per candidate before reviewing; (2) `fleet-claim` blocker-check uses `git merge-base --is-ancestor <pred-branch-tip> origin/master` to resolve blockers independent of TASKS.md state; (3) queue-manager maintenance pass detects `[~]` tasks whose branch is an ancestor of master (empty diff) and auto-flips to `[x]` with merged-PR cross-reference; (4) script/doc changes; no engine build required
  - **Issue:** #387
  - **Notes:** Three issues: D2 opus-reviewer relaunched 2.4m after previous iteration (30m floor not enforced) burning ~5m context; D3 T-053 unclaimable 5–15m after T-051 merged because fleet-claim reads TASKS.md not git; D4 T-064 had stale `[~]` status after PR #349 merged — empty-diff PR #383 opened and immediately closed.
  - **Links:**

- [ ] **Review-pr: detect oversized churn on CONFLICTING PRs + forked-from-other-PR signal** — add `gh pr diff --stat` check when mergeable==CONFLICTING; add fleet:fork-of-other-pr label when merger detects branch forked from another open PR
  - **ID:** T-081
  - **Area:** tooling, docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `review-pr/SKILL.md` runs `gh pr diff <N> --stat` when `mergeable == CONFLICTING`; flags files with >100 lines churn not in PR's claimed file list; (2) merger adds `fleet:fork-of-other-pr` label (or repurposes `fleet:awaiting-base` with updated description) when detecting fork condition; (3) opus-worker step 1c excludes `fleet:fork-of-other-pr` same as `fleet:awaiting-base`; (4) CLAUDE.md labeling section documents new label; doc + label creation; no engine build required
  - **Issue:** #388
  - **Notes:** Two issues: E1 PR #382 (T-065) would have silently reverted ~590 lines of PR #368 (lighting fidelity polish) if merged — the `mergeable: CONFLICTING` status was the signal; Sonnet missed it because no `gh pr diff --stat` check exists; E2 PR #378 (T-063) was forked from `claude/T-062-lambda-decay` branch (not master), carrying T-062's 10 commits — `fleet:semantic-conflict` set by merger was misleading; the right resolution is `git rebase --onto origin/master <T-062-tip> <T-063-branch>` once T-062 merges. Detect via `git merge-base PR-head other-PR-head == other-PR-head`.
  - **Links:**

- [ ] **Fleet: factor CLAUDE.md status-prose sections to prevent parallel-PR rebase conflicts** — move rapidly-changing status prose from feature-PR-editable CLAUDE.md sections into queue-manager-owned file(s); update worker docs to restate only changed lines when editing shared status sections
  - **ID:** T-082
  - **Area:** tooling, docs
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) rapidly-changing status prose (e.g. "Open follow-ups", "Runtime gaps", "ships X of Y systems") factored out of CLAUDE.md sections that feature PRs touch, into queue-manager-owned file(s); OR `simplify/SKILL.md` flags diffs touching >2 paragraphs of a shared status section as conflict-prone; (2) worker docs updated: when editing CLAUDE.md "Open follow-ups"/"Status" sections, restate only changed lines; (3) path chosen (option 1 or 2) documented in `CLAUDE.md` or relevant role doc
  - **Issue:** #389
  - **Notes:** Root cause: T-061/T-060/T-062 all authored against pre-T-061 master; all three rewrote rapidly-changing status prose in `engine/prefabs/irreden/common/CLAUDE.md`; both #348 and #351 got `fleet:semantic-conflict` for opus-worker resolution. Three fix options in issue; recommend option 1 (same shape as splitting per-task plans into `~/.fleet/plans/`). Option 2 is cheaper but relies on agent compliance.
  - **Links:**

---

## In progress

<!-- Tasks currently being worked on. Mirror of [~] items above. -->


---

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-063** — Fleet: design-escalation flow — bidirectional labels + plan re-sync + role docs · Owner: claude/T-063-design-escalation · PR: https://github.com/jakildev/IrredenEngine/pull/378
- [x] **T-062** — Modifier framework: lambda decay system + stateful-lambda design · Owner: claude/T-062-lambda-decay · PR: https://github.com/jakildev/IrredenEngine/pull/351
- [x] **T-053** — Modifier framework: modifier_demo creation (visual showcase) · Owner: claude/T-053-modifier-demo · PR: https://github.com/jakildev/IrredenEngine/pull/377
- [x] **T-064** — engine/system docs: document 'no function-local static for system state' rule · Owner: claude/T-064-system-static-docs · PR: https://github.com/jakildev/IrredenEngine/pull/349
- [x] **T-060** — Modifier framework: wire MODIFIER_RESOLVE_EXEMPT via archetype exclude-tag filter · Owner: claude/T-060-exclude-tag-filter · PR: https://github.com/jakildev/IrredenEngine/pull/348
- [x] **T-061** — Modifier framework: pre-destroy hook for auto-sweep of source-attributed modifiers · Owner: claude/T-061-pre-destroy-hook · PR: https://github.com/jakildev/IrredenEngine/pull/347
- [x] **T-059** — Fleet docs: dormancy-verification rule for private creations · Owner: claude/T-059-dormancy-check · PR: https://github.com/jakildev/IrredenEngine/pull/346
- [x] **T-051** — Modifier framework: migrate position + velocity-drag patterns · Owner: claude/T-051-modifier-position-velocity · PR: https://github.com/jakildev/IrredenEngine/pull/332
- [x] **T-052** — Modifier framework: Lua bindings · Owner: claude/T-052-lua-bindings · PR: https://github.com/jakildev/IrredenEngine/pull/331
- [x] **T-054** — Render: world Z-yaw view/camera transform foundation (C_CameraYaw, cardinal/residual split, GPU feeders) · Owner: claude/T-054-camera-yaw · PR: https://github.com/jakildev/IrredenEngine/pull/327
- [x] **T-050** — Modifier framework: core runtime (registry, 5 resolver systems, source sweep) · Owner: claude/T-050-modifier-runtime · PR: https://github.com/jakildev/IrredenEngine/pull/325
- [x] **T-048** — CLAUDE.md sharing mechanism: baseline file + per-creation opt-out · Owner: claude/T-048-claude-md-sharing · PR: https://github.com/jakildev/IrredenEngine/pull/320
- [x] **T-046** — Audit: component-with-helper patterns across engine prefabs, codify rules · Owner: claude/T-046-component-helper-audit · PR: https://github.com/jakildev/IrredenEngine/pull/319
- [x] **T-045** — Fleet: stacked-PR: TASKS.md Stack: field for chain visibility · Owner: claude/T-045-stack-field-task-template · PR: https://github.com/jakildev/IrredenEngine/pull/318
- [x] **T-049** — Modifier framework: design doc + audit + framework declarations · Owner: claude/T-049-modifier-framework-foundation · PR: https://github.com/jakildev/IrredenEngine/pull/315
- [x] **T-047** — Engine CLAUDE.md style: add "prefer enums over strings" rule · Owner: claude/T-047-enum-style-rule · PR: https://github.com/jakildev/IrredenEngine/pull/314
- [x] **T-044** — Fleet: stacked-PR: downstream auto-rebase when upstream changes · Owner: claude/T-044-rebase-downstream · PR: https://github.com/jakildev/IrredenEngine/pull/308
- [x] **T-043** — Fleet: stacked-PR: reviewer upstream approval gating · Owner: claude/T-043-reviewer-upstream-gating · PR: https://github.com/jakildev/IrredenEngine/pull/301
- [x] **T-040** — Fleet: trigger-aware back-off in fleet-babysit · Owner: claude/T-040-trigger-aware-backoff · PR: https://github.com/jakildev/IrredenEngine/pull/300
- [x] **T-039** — Fleet: roles read scout cache instead of running gh/git directly · Owner: claude/T-039-roles-read-scout-cache · PR: https://github.com/jakildev/IrredenEngine/pull/296
