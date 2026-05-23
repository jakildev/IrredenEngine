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
requeue with `[opus]`. [`docs/agents/FLEET.md`](docs/agents/FLEET.md) "Model split" has the full split.

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

- [~] **Render: HDR pipeline — RGBA16F canvas, tonemap pass, exposure control, sky term** — grow LDR pipeline into HDR; RGBA16F canvas color attachment; tonemap pass between LIGHTING_TO_TRIXEL and TRIXEL_TO_FRAMEBUFFER; exposure uniform; additive sky-term from emissive top hemisphere
  - **ID:** T-118
  - **Area:** engine/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** claude/T-118-hdr-pipeline
  - **Blocked by:** (none)
  - **Acceptance:** (1) bright emissive lights no longer clip at white; saturation preserved through lighting → tonemap chain; (2) new lighting demo (IRLightingHDR or similar) exercises full HDR pipeline; (3) existing lighting demos (IRLightingCombined, IRLightingPoint, IRLightingSpot, IRLightingEmissive, IRLightingSunShadow) look identical to pre-HDR LDR output at default exposure; (4) fleet-build clean on linux-debug AND macos-debug
  - **Issue:** #366
  - **Notes:** Follow-up from lighting-fidelity-polish PR (audit findings #35-#38). Not in the lighting-fidelity-polish PR because HDR is a separate correctness dimension requiring its own tonemap tuning, demo screenshots, and perf measurement. Pick one tonemap operator and ship it (Reinhard, ACES, or Uncharted-2). Sky term: emissive top hemisphere driving additive contribution that cuts off at occlusion — cheap and visually impactful.
  - **Links:**


- [~] **Render: low game-resolution mode + sub-pixel / screen-pixel camera smoothing** — restore low-resolution rendering with anti-vibration sub-pixel camera smoothing and a per-entity screen-pixel-precision opt-in
  - **ID:** T-314
  - **Area:** engine/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** claude/T-314-lowres-subpixel
  - **Blocked by:** (none)
  - **Acceptance:** (1) slow camera pan at low game resolution shows no per-frame +/-1px jitter in auto-screenshot diff; (2) game-resolution canvases snap to game-pixel grid; camera tracks at screen-pixel precision; (3) screen-pixel-smooth flagged entity moves smoothly between game pixels; unflagged entities snap; (4) fleet-build clean on linux-debug and macos-debug; OpenGL/Metal parity for any shader change
  - **Issue:** #1048
  - **Notes:** Audit system_framebuffer_to_screen.hpp existing sub-pixel path first; anti-vibration rule: decompose camera position once into game-pixel-integer + screen-pixel-remainder and use consistently throughout. Per-entity screen-pixel-smooth opt-in routes through existing SPRITE_TO_SCREEN post-upscale pass. kSizeExtraPixelBuffer margin must cover max one-frame shift. Demo at 320x180 slow pan. Filed by architect (Cursor session).
  - **Links:**


- [~] **Perf: VOXEL_TO_TRIXEL_STAGE_1 per-frame canvas+distance clear** — eliminate ~4.4 ms/frame CPU→GPU texture clear by switching to GPU-side clear or a persistent scratch buffer
  - **ID:** T-315
  - **Area:** engine/render, shaders/metal
  - **Model:** sonnet
  - **Owner:** claude/T-315-gpu-side-canvas-clear
  - **Blocked by:** (none)
  - **Acceptance:** vs1_clear scope in perf_grid auto-profile drops well under 1 ms; render-verify passes with no visual regression
  - **Issue:** #1050
  - **Notes:** Metal clearTexImage allocates a full-texture std::vector + memset + CPU replaceRegion each frame for the distance texture. Fix: GPU-side compute/blit clear or load-action clear. Also cross-check OpenGL clearTexImage path for same pattern. vs1_clear IR_PROFILE_SCOPE was added in PR #1049.
  - **Links:**


- [ ] **Perf: worker_threads axis + entity-count override in perf_grid_matrix** — pre-phase-1 measurement surface for multithreading epic; adds worker_threads axis and entity_count_override to perf_grid_matrix.sh, WorldConfig, and report emitters; files threading baseline doc
  - **ID:** T-220
  - **Area:** build, engine/world, docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Stack:** T-220..T-225 ecs-multithreading
  - **Acceptance:** perf-gate.yml runs threading-baseline matrix on master; docs/perf-reports/threading_baseline.md exists with {4K, 32K, 262K} × {0, 1, hw-2} cells (all serial today); existing perf-gate cells pass with no regression
  - **Issue:** #1067
  - **Notes:** Part of multithreading epic #226. Adds worker_threads axis (values 0, 1, hw_concurrency()-2) and entity_count_override field to WorldConfig; worker_thread_count itself lands in T-221. Per-worker utilization columns stay stubbed until enkiTS is in. Don't add worker_thread_count to WorldConfig here. Lands in parallel with T-221.
  - **Links:**


- [~] **Job: vendor enkiTS + access-derivation traits** — stand up engine/job/ static lib with enkiTS, public IRJobs API, worker lifecycle hooks, and SystemAccess derivation traits in engine/system/
  - **ID:** T-221
  - **Area:** engine/job, engine/system, engine/world
  - **Model:** opus
  - **Owner:** opus-worker-2
  - **Blocked by:** (none)
  - **Stack:** T-220..T-225 ecs-multithreading
  - **Acceptance:** engine/job/ builds clean on linux-debug and macos-debug; IRJobs::parallelFor(0,1024,256,...) smoke-test runs reporting expected worker IDs; IRJobs::isMainThread() true from main thread / false from workers; deriveAccessFromSignature unit tests cover all three tick-signature forms; T-220's perf-gate baseline passes; Apple Silicon logs P-core cap decision at startup
  - **Issue:** #1068
  - **Notes:** Phase 1 of #226. Vendors enkiTS at engine/job/third_party/enkiTS/ (zlib license). Public API: parallelFor, run, pinTo, isMainThread, workerId, workerCount. WorldConfig gains worker_thread_count (default max(1, hw-2)); on Apple Silicon cap to P-core count via sysctlbyname. Worker startup: EASY_THREAD + seed thread-local RNG. Pool alive between World::start() and World::stop(). AccessDerivation traits (SystemAccess, tag types, deriveAccessFromSignature) pure unit-test surface — unused at this phase. T-222 and T-223 depend on this.
  - **Links:**


- [ ] **System: Concurrency::PARALLEL_FOR + single-system access validation** — land actual per-tick parallel dispatch, registration-time safety checks, IR_ASSERT_MAIN_THREAD guards, and three POC system ports
  - **ID:** T-222
  - **Area:** engine/system, creations/demos
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-221
  - **Stack:** T-220..T-225 ecs-multithreading
  - **Acceptance:** perf_grid_matrix.sh 262K cell shows ≥2× UPDATE throughput at worker_threads=hw-2 vs 0; VELOCITY_3D/VELOCITY_DRAG/ANIMATION_COLOR pass IrredenEngineTest; unsafe parallel systems (EntityId+no-ParallelSafe, batch-form) rejected at registration (unit-tested); IR_ASSERT_MAIN_THREAD fires from worker thread in debug (unit-tested); no regression on T-220's perf-gate
  - **Issue:** #1069
  - **Notes:** Phase 2 of #226. Extends createSystem with Concurrency enum (SERIAL, PARALLEL_FOR, MAIN_THREAD; default SERIAL). PARALLEL_FOR: IRJobs::parallelFor per archetype node, grainSize=512 (tunable, not auto-tuned). beginTick/endTick always serial on main thread. IR_ASSERT_MAIN_THREAD wired into RenderManager, IRRender::*, AudioManager, IRAudio::*, IRVideo::*, LuaScript::*, sol2 bindings. POC ports chosen for no EntityId param and no manager calls; don't port a fourth system in this PR.
  - **Links:**


- [ ] **Script: Lua codegen + EVAL concurrency integration** — extend Lua DSL with concurrency field; CODEGEN maps to Concurrency enum; EVAL mode overrides to MAIN_THREAD with warning
  - **ID:** T-223
  - **Area:** engine/script, creations/demos/lua_perf_grid
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-221, T-222
  - **Stack:** T-220..T-225 ecs-multithreading
  - **Acceptance:** lua_perf_grid (CODEGEN) at 262K entities with worker_threads=hw-2 within ±10% of C++ perf_grid; EVAL mode with concurrency="parallel_for" warns clearly and runs serial; codegen tool errors on bogus concurrency value; Lua parallel_for + EntityId first param gets registration-time FATAL
  - **Issue:** #1070
  - **Notes:** Phase 2.5 of #226 — Lua-driven ECS parallelism. Adds concurrency field to codegen DSL (serial/parallel_for/main_thread; default serial keeps all existing specs working). EVAL path overrides to MAIN_THREAD with one-time warning per system (sol2 not thread-safe; LuaJIT GC/JIT state not thread-safe). Do NOT expose IRJobs::parallelFor to Lua directly. Access-derivation trait runs automatically on codegen-emitted C++ lambda. Blocked by both T-221 (traits) and T-222 (Concurrency enum).
  - **Links:**


- [ ] **System: pipeline groups + cross-system access validation** — add registerPipelineGroups API and registration-time cross-system conflict validator; migrate engine UPDATE pipeline
  - **ID:** T-224
  - **Area:** engine/system
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-222
  - **Stack:** T-220..T-225 ecs-multithreading
  - **Acceptance:** validator rejects: conflicting-write group (unit-tested), MAIN_THREAD system in group (unit-tested), two spawners in group (unit-tested); engine UPDATE pipeline reorganized; IRShapeDebug --auto-screenshot 60 unchanged; perf_grid_matrix shows additional speedup beyond T-222's number
  - **Issue:** #1071
  - **Notes:** Phase 3 of #226. registerPipelineGroups(pipeline, {{a,b,c},{d},{e,f}}) API; inner braces are parallel groups (concurrent via IRJobs); groups run sequentially. std::list<SystemId> per pipeline becomes std::vector<std::vector<SystemId>>. Cross-system conflict check at World::start(): writes∩reads, writes∩writes, MAIN_THREAD in group, two mutates_archetype_graph in group (last restriction lifted by T-225). Error messages must name both systems + conflicting component. Two-spawners-in-same-group restriction lifted in T-225.
  - **Links:**


- [ ] **Entity: thread-safe deferred mutations from worker threads** — per-worker staging buffers in EntityManager; lift T-224's two-spawners-in-same-group restriction
  - **ID:** T-225
  - **Area:** engine/entity, engine/system
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-224
  - **Stack:** T-220..T-225 ecs-multithreading
  - **Acceptance:** stress test: PARALLEL_FOR system spawning 10K entities across all workers produces same archetype graph as serial; stress test: concurrent destruction of 10K entities across workers produces correct null state; T-224 validator accepts group containing two Spawns systems (unit-tested); no regression on T-222's ≥2× speedup
  - **Issue:** #1072
  - **Notes:** Phase 4 of #226. Per-worker staging in EntityManager: setComponentDeferred, removeComponentDeferred, markEntityForDeletion, createEntity (deferred) backed by per-worker buffers indexed by IRJobs::workerId(). Main thread uses buffer 0. Drain in flushStructuralChanges() (existing serial fence). createEntity uses atomic counter for unique IDs (not per-worker ranges — avoids sparse archetype index). Drain order deterministic (workerId order) for auto-screenshot reproducibility. Lifts mutates_archetype_graph conflict check from T-224.
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-316** — render: skip Metal buffer orphan when GPU is idle · Owner: claude/T-316-metal-buffer-no-orphan · PR: https://github.com/jakildev/IrredenEngine/pull/1065
- [x] **T-317** — camera-rotation controls — canvas_stress auto-rotate + Ctrl+middle-drag rotate · Owner: claude/T-317-camera-rotation · PR: https://github.com/jakildev/IrredenEngine/pull/1063
- [x] **T-302** — retire C_Position3D / C_PositionGlobal3D / C_Rotation legacy components · Owner: claude/T-302-retire-legacy-position-rotation · PR: https://github.com/jakildev/IrredenEngine/pull/1062
- [x] **T-295** — DETACHED canvas SO(3) rotation · Owner: claude/t295-canvas-so3 · PR: https://github.com/jakildev/IrredenEngine/pull/1047
- [x] **T-313** — perf: Lua-vs-C++ parity dashboard · Owner: claude/T-313-lua-cpp-parity-dashboard · PR: https://github.com/jakildev/IrredenEngine/pull/1037
- [x] **T-311** — perf: CI baseline + automated regression gate for engine/render, engine/system, engine/math PRs · Owner: claude/T-311-ci-baseline-gate · PR: https://github.com/jakildev/IrredenEngine/pull/1039
- [x] **T-305** — math: IRMath::SDF::evaluateGrid batch helper + refactor applyFillSDF · Owner: claude/T-305-sdf-grid-batch · PR: https://github.com/jakildev/IrredenEngine/pull/1029
- [x] **T-307** — skills: decompose /simplify into parallel reuse-detection subagents · Owner: claude/T-307-simplify-subagent-decomposition · PR: https://github.com/jakildev/IrredenEngine/pull/1040
- [x] **T-309** — render: split visible vs shadow-feeder voxel compaction (design doc) · Owner: claude/T-309-feeder-split · PR: https://github.com/jakildev/IrredenEngine/pull/1036
- [x] **T-310** — render: graceful per-pair fallback for Metal timestamp allocation · Owner: claude/T-310-async-gpu-timers · PR: https://github.com/jakildev/IrredenEngine/pull/1035
- [x] **T-312** — perf: Catch2 microbench harness for engine/math hot paths · Owner: claude/T-312-math-microbench-harness · PR: https://github.com/jakildev/IrredenEngine/pull/1034
- [x] **T-308** — demos: named config preset files (IRPerfGrid + friends) · Owner: claude/T-308-config-preset-flag · PR: https://github.com/jakildev/IrredenEngine/pull/1032
- [x] **T-304** — render: extract mask-grid pixel packing into renderer helper · Owner: claude/T-304-render-mask-grid-helper · PR: https://github.com/jakildev/IrredenEngine/pull/1031
- [x] **T-306** — asset: scene_io metadata index + voxel-record byte constant dedup · Owner: claude/T-306-scene-io-metadata-index · PR: https://github.com/jakildev/IrredenEngine/pull/1030
- [x] **T-303** — math: IRMath grid-iteration and 3D-mask helpers · Owner: claude/T-303-irmath-grid-helpers · PR: https://github.com/jakildev/IrredenEngine/pull/1028
- [x] **T-293** — render geometric trixel deformation (replaces T-322 bilinear residual) · Owner: claude/T-293-geometric-trixel-deformation · PR: https://github.com/jakildev/IrredenEngine/pull/1005
- [x] **T-294** — SYSTEM_REBUILD_GRID_VOXELS runs on entities with changed C_LocalTransform; rotates authored voxels to world-grid cells; last-writer-wins on cell collisions (deterministic by entity ID) · Owner: (auto-reaped) · PR: https://github.com/jakildev/IrredenEngine/issues/957
- [x] **T-291** — wire detached canvas rotation through composite TRS (C3) · Owner: claude/T-291-detached-canvas-rotation · PR: https://github.com/jakildev/IrredenEngine/pull/1003
- [x] **T-292** — math: continuous-yaw + deformation math helpers (C5) · Owner: claude/T-292-yaw-deformation-math · PR: https://github.com/jakildev/IrredenEngine/pull/1002
- [x] **T-301** — migrate C_VoxelPool and ~10 voxel-pipeline files off legacy position components; architect call required on pool SoA layout (Option A: C_WorldTransform array vs Option B: position-only projection arrays) · Owner: (auto-reaped) · PR: https://github.com/jakildev/IrredenEngine/issues/986
