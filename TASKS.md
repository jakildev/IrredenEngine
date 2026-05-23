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


- [~] **System: Concurrency::PARALLEL_FOR + single-system access validation** — land actual per-tick parallel dispatch, registration-time safety checks, IR_ASSERT_MAIN_THREAD guards, and three POC system ports
  - **ID:** T-222
  - **Area:** engine/system, creations/demos
  - **Model:** opus
  - **Owner:** claude/T-222-parallel-for-validation
  - **Blocked by:** (none)
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
  - **Blocked by:** T-222
  - **Stack:** T-220..T-225 ecs-multithreading
  - **Acceptance:** lua_perf_grid (CODEGEN) at 262K entities with worker_threads=hw-2 within ±10% of C++ perf_grid; EVAL mode with concurrency="parallel_for" warns clearly and runs serial; codegen tool errors on bogus concurrency value; Lua parallel_for + EntityId first param gets registration-time FATAL
  - **Issue:** #1070
  - **Notes:** Phase 2.5 of #226 — Lua-driven ECS parallelism. Adds concurrency field to codegen DSL (serial/parallel_for/main_thread; default serial keeps all existing specs working). EVAL path overrides to MAIN_THREAD with one-time warning per system (sol2 not thread-safe; LuaJIT GC/JIT state not thread-safe). Do NOT expose IRJobs::parallelFor to Lua directly. Access-derivation trait runs automatically on codegen-emitted C++ lambda. Blocked by both T-221 (traits) and T-222 (Concurrency enum).
  - **Links:**


- [~] **System: pipeline groups + cross-system access validation** — add registerPipelineGroups API and registration-time cross-system conflict validator; migrate engine UPDATE pipeline
  - **ID:** T-224
  - **Area:** engine/system
  - **Model:** opus
  - **Owner:** claude/T-224-pipeline-groups
  - **Blocked by:** T-222
  - **Stack:** T-220..T-225 ecs-multithreading
  - **Acceptance:** validator rejects: conflicting-write group (unit-tested), MAIN_THREAD system in group (unit-tested), two spawners in group (unit-tested); engine UPDATE pipeline reorganized; IRShapeDebug --auto-screenshot 60 unchanged; perf_grid_matrix shows additional speedup beyond T-222's number
  - **Issue:** #1071
  - **Notes:** Phase 3 of #226. registerPipelineGroups(pipeline, {{a,b,c},{d},{e,f}}) API; inner braces are parallel groups (concurrent via IRJobs); groups run sequentially. std::list<SystemId> per pipeline becomes std::vector<std::vector<SystemId>>. Cross-system conflict check at World::start(): writes∩reads, writes∩writes, MAIN_THREAD in group, two mutates_archetype_graph in group (last restriction lifted by T-225). Error messages must name both systems + conflicting component. Two-spawners-in-same-group restriction lifted in T-225.
  - **Links:**


- [~] **Entity: thread-safe deferred mutations from worker threads** — per-worker staging buffers in EntityManager; lift T-224's two-spawners-in-same-group restriction
  - **ID:** T-225
  - **Area:** engine/entity, engine/system
  - **Model:** opus
  - **Owner:** claude/T-225-parallel-spawn
  - **Blocked by:** T-224
  - **Stack:** T-220..T-225 ecs-multithreading
  - **Acceptance:** stress test: PARALLEL_FOR system spawning 10K entities across all workers produces same archetype graph as serial; stress test: concurrent destruction of 10K entities across workers produces correct null state; T-224 validator accepts group containing two Spawns systems (unit-tested); no regression on T-222's ≥2× speedup
  - **Issue:** #1072
  - **Notes:** Phase 4 of #226. Per-worker staging in EntityManager: setComponentDeferred, removeComponentDeferred, markEntityForDeletion, createEntity (deferred) backed by per-worker buffers indexed by IRJobs::workerId(). Main thread uses buffer 0. Drain in flushStructuralChanges() (existing serial fence). createEntity uses atomic counter for unique IDs (not per-worker ranges — avoids sparse archetype index). Drain order deterministic (workerId order) for auto-screenshot reproducibility. Lifts mutates_archetype_graph conflict check from T-224.
  - **Links:**


- [~] **render: retire SCREEN_SPACE_RESIDUAL_ROTATE passthrough stage** — delete the passthrough system, dedicated shader pair, and UBO struct; replace every consumer with FRAMEBUFFER_TO_SCREEN
  - **ID:** T-323
  - **Area:** engine/render, engine/prefabs/irreden/render, shaders/glsl, shaders/metal
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-2
  - **Blocked by:** (none)
  - **Acceptance:** (1) grep -rn SCREEN_SPACE_RESIDUAL_ROTATE engine creations returns zero hits; (2) every demo that used the stage renders identically (render-verify or auto-screenshot diff); (3) voxel_to_trixel_stage_1/stage_2 shaders gain retirement note referencing T-293; (4) fleet-build clean linux-debug and macos-debug
  - **Issue:** #1079
  - **Notes:** Stage is a passthrough since T-293 folded residualYaw into faceDeform_. Audit all of creations/ (including private creations/game/) for consumers before deletion. Delete: v_screen_residual_rotate.glsl, f_screen_residual_rotate.glsl, Metal twins, FrameDataScreenResidualRotate UBO, ScreenSpaceResidualRotateProgram + ScreenSpaceResidualRotateFrameData named resources, SCREEN_SPACE_RESIDUAL_ROTATE SystemName entry. Independent of #1075 and camera-SO(3) work.
  - **Links:**


- [~] **System: complete T-222 POC ports + SystemAccess tag-shadow fix** — fix per-worker RNG, migrate ANIMATION_COLOR static caches, port VELOCITY_DRAG + ANIMATION_COLOR to PARALLEL_FOR, fix tag-shadow in SystemAccess deriveAccessFromSignature; address const C_Foo dispatch UB
  - **ID:** T-328
  - **Area:** engine/system, engine/math, engine/prefabs/irreden/update
  - **Model:** opus
  - **Owner:** claude/T-328-system-poc-ports-systemaccess-fix
  - **Blocked by:** T-222
  - **Acceptance:** randomFloat/randomBool/randomInt route through IRJobs::workerRng() (unit-tested); ANIMATION_COLOR static caches replaced with member fields on registerSystem form; VELOCITY_DRAG + ANIMATION_COLOR opt in to PARALLEL_FOR where safe; deriveAccessFromSignature correctly handles tag-bearing packs via TypeList filter; T-222 validator workaround in system_concurrency_test.cpp replaced with proper deriveAccessFromSignature call; const C_Foo dispatch path covered by unit test
  - **Issue:** #1096
  - **Notes:** Deferred from T-222 (PR #1097). Sub-tasks: A) route rand()-based IRMath::randomFloat/randomBool/randomInt through per-worker IRJobs::workerRng() mt19937; B) ANIMATION_COLOR: move colorTrackCache/clipCache to member fields, convert to registerSystem + tick() form; C) VELOCITY_DRAG + ANIMATION_COLOR PARALLEL_FOR opt-in after A+B (replace std::sin/std::abs with IRMath in VELOCITY_DRAG diff); D) ~30 LOC TypeList filter to partition tag types out of component pack before invocability probes; E) unit test exposing const C_Foo dispatch UB (see issue comment from jakildev). Lands before T-223.
  - **Links:**


- [~] **Tools: ir-perf-grid + hardware-fingerprinted baselines + synthetic-load normalization (sub-task 3 of #1074)** — ir-perf-grid binary wrapping ir-acquire benchmark; baselines keyed by host fingerprint; ir_ref_bench normalization for cross-host comparisons
  - **ID:** T-330
  - **Area:** engine/tools/bin, engine/tools/bench, engine/tools/py, docs/perf, build
  - **Model:** opus
  - **Owner:** claude/T-330-ir-perf-grid
  - **Blocked by:** (none)
  - **Stack:** T-318..T-331 ir-tools-split
  - **Acceptance:** ir-perf-grid runs matrix end-to-end, writes baseline under fingerprinted layout; two consecutive runs on same host produce stable raw + normalized numbers (within 2%); different-host run emits informational host-mismatch path, not regression alert; perf-gate.yml passes against existing master baseline after migration
  - **Issue:** #1100
  - **Notes:** Sub-task 3 of #1074. Three deliverables: (1) ir-perf-grid binary wrapping matrix in ir-acquire benchmark + running ir_ref_bench for ref_ms; (2) baseline reorg to docs/perf/baseline_latest/<fingerprint>/ with host.json sidecars; (3) ir_ref_bench.cpp (~50ms IRMath isoToScreen + SDF + trixel bench). Comparator: same fingerprint → gate fires; different → informational; no match → seed baseline. Contended lock: weight normalized over raw. Calibration cache at ~/.cache/irreden/calibration/<fingerprint>.json; invalidated when engine/math/ SHA changes or max_age_days exceeded.
  - **Links:**


- [~] **Docs: acquire-late, release-early rule in worker-role docs (sub-task 4 of #1074)** — canonical lock-discipline rule in FLEET.md; one-line pointer from each of the three worker role docs
  - **ID:** T-331
  - **Area:** docs, .claude/commands
  - **Model:** sonnet
  - **Owner:** claude/T-331-acquire-late-release-early-docs
  - **Blocked by:** (none)
  - **Stack:** T-318..T-331 ir-tools-split
  - **Acceptance:** rule lives in exactly one canonical place (FLEET.md § "Resource coordination" or engine/tools/README.md); each of role-opus-worker.md, role-sonnet-author.md, role-opus-architect.md references it; no duplicated prose; grep confirms single canonical location
  - **Issue:** #1101
  - **Notes:** Sub-task 4 of #1074. Doc-only. Rule text per issue: acquire immediately before the operation that needs it; release immediately after; never hold across decide/draft/read-feedback steps; perf measurement holds perf lock for perf-grid run only. Should land after T-329 so the rule references a wired tool.
  - **Links:**


- [~] **System: migrate UPDATE pipeline to multi-system groups + measure perf_grid_matrix speedup (T-224 follow-up)** — const-correctness audit of prefab systems to unlock real parallel groups; run perf matrix and file speedup report
  - **ID:** T-332
  - **Area:** engine/system, creations/demos
  - **Model:** opus
  - **Owner:** opus-worker-1
  - **Blocked by:** T-224
  - **Acceptance:** at least one demo's UPDATE pipeline has a real parallel group accepted by the T-224 validator; perf_grid_matrix.sh shows additional speedup beyond T-222's baseline; speedup number filed to docs/perf-reports/threading_phase3.md; no regressions on existing demos
  - **Issue:** #1103
  - **Notes:** Deferred from T-224 (infrastructure landed; migration + measurement deferred). Gating work: most prefab systems haven't declared const opt-in on tick params; validator conservatively classifies all reads as writes and rejects every multi-system grouping. Per-prefab const-correctness audit is required first. Part of epic #226.
  - **Links:**


- [ ] **System: pre-resolve component-vector refs on main thread before PARALLEL_FOR dispatch** — eliminate operator[] race on EntityManager from worker threads; pre-bind component vectors before parallelFor call
  - **ID:** T-333
  - **Area:** engine/system, engine/entity
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-222
  - **Acceptance:** worker bodies under PARALLEL_FOR no longer call m_pureComponentTypes::operator[]; ThreadSanitizer-clean parallelFor dispatch over archetype with >=kDefaultGrainSize entities; VELOCITY_3D continues to tick correctly
  - **Issue:** #1105
  - **Notes:** Deferred from T-222 review (PR #1097, Opus recheck nit #1). Latent UB: getComponentData<C>(node) calls m_pureComponentTypes[typeName] (operator[], non-const) from each worker. Works today on libstdc++/libc++/MSVC by coincidence; real race as PARALLEL_FOR broadens (T-223, T-225). Cleanest fix: pre-resolve component vectors in SystemManager::executeSystem before IRJobs::parallelFor, pass as std::tuple into rangedFn. Smaller fix: switch to m_pureComponentTypes.at(typeName). Part of epic #226. Should land before T-223.
  - **Links:**


- [ ] **System: PARALLEL_FOR + relation-form validator gap** — add validator rule rejecting PARALLEL_FOR systems using relation-form tick; add SystemAccess::isRelationForm_ bit
  - **ID:** T-334
  - **Area:** engine/system
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-222
  - **Acceptance:** validateConcurrencyForAccess rejects PARALLEL_FOR + relation-form at registration time; unit test in system_concurrency_test.cpp confirms rejection (mirrors BatchFormRejected shape); existing relation-form systems (all currently SERIAL) tick unchanged
  - **Issue:** #1106
  - **Notes:** Deferred from T-222 review (PR #1097, Opus recheck nit #2). rangedFn's relation branch calls getRelatedEntityFromArchetype + getComponentOptional<RelComps> — both EntityManager hash-map lookups that race under PARALLEL_FOR. Not a real bug today (no system combines relations + PARALLEL_FOR) but must land before T-223 broadens rollout. Preferred fix: add isRelationForm_ bit to SystemAccess (set from relation-form trait branch), add validator rule. Part of epic #226.
  - **Links:**


- [~] **Test/System: integration test that PARALLEL_FOR dispatch parallelizes and processes every row** — fixture ticks PARALLEL_FOR system over >=kDefaultGrainSize entities, asserts every entity processed exactly once
  - **ID:** T-335
  - **Area:** engine/system, test/system
  - **Model:** sonnet
  - **Owner:** claude/T-335-parallel-dispatch-test
  - **Blocked by:** T-222
  - **Acceptance:** new test in test/system/ ticks PARALLEL_FOR system over >=kDefaultGrainSize entities and asserts every row processed exactly once; TSAN-friendly variant using vector<atomic<int>> catches worker overlap; existing 894 tests pass; optional: test confirms PARALLEL_FOR + relation-form rejected at registration (requires T-334)
  - **Issue:** #1107
  - **Notes:** Deferred from T-222 review (PR #1097, Opus recheck nit #3). Core test: spin up EntityManager + SystemManager + JobManager(2), register PARALLEL_FOR system with atomic counter tick body, populate 4096 entities (forces multiple kDefaultGrainSize=512 chunks), tick once, assert counter==4096. Optional relation-form rejection test depends on T-334 landing first. Part of epic #226.
  - **Links:**


- [~] **Investigate + fix macOS demo segfault/non-clean shutdown** — reproduce the shutdown crash on macOS (e.g. IRPerfGrid); identify root cause; apply targeted fix; harden run/verify tooling to validate exit codes
  - **ID:** T-336
  - **Area:** engine/render, creations/demos
  - **Model:** opus
  - **Owner:** opus-worker-1
  - **Blocked by:** (none)
  - **Acceptance:** IRPerfGrid and at least two other representative demos exit cleanly on macOS (exit code 0, no crash reporter dialog); fix verified on macos-debug preset; fleet-build / ir-run wrapper does not mask the non-zero exit code; no regression on linux-debug
  - **Issue:** #1116
  - **Notes:** Reported on macOS; IRPerfGrid cited as one reproducer. Root cause unknown — may involve Metal resource teardown order, ECS world destructor sequencing, or missing signal handler. Also investigate whether run/verify skills should assert clean exit code after demo auto-screenshot runs.
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-329** — tools: ir-build / ir-run wrappers with ir-acquire wiring · Owner: claude/T-329-ir-build-run · PR: https://github.com/jakildev/IrredenEngine/pull/1111
- [x] **T-326** — demos: adopt standardControlSystems() bundle across all demos · Owner: claude/T-326-adopt-standard-camera-bundle · PR: https://github.com/jakildev/IrredenEngine/pull/1095
- [x] **T-221** — engine/job/ + SystemAccess traits (multithreading epic Phase 1) · Owner: claude/T-221-job-foundation · PR: https://github.com/jakildev/IrredenEngine/pull/1086
- [x] **T-318** — engine/tools: ir-host-probe + ir-acquire (sub-task 1 of #1074) · Owner: claude/T-318-engine-tools · PR: https://github.com/jakildev/IrredenEngine/pull/1102
- [x] **T-327** — broaden cross-host smoke criteria; add windows-* + verified-* labels · Owner: claude/T-327-cross-host-smoke-windows · PR: https://github.com/jakildev/IrredenEngine/pull/1098
- [x] **T-325** — engine/prefabs/render: unified camera-controls bundle + trackpad gesture support · Owner: claude/T-325-camera-controls-bundle · PR: https://github.com/jakildev/IrredenEngine/pull/1094
- [x] **T-320** — docs: iso-depth-axis invariant design doc · Owner: claude/T-320-iso-depth-axis-invariant · PR: https://github.com/jakildev/IrredenEngine/pull/1090
- [x] **T-324** — editors/voxel_editor: migrate EditorViewportRotate to shared-ptr state capture · Owner: claude/T-324-editor-viewport-rotate · PR: https://github.com/jakildev/IrredenEngine/pull/1089
- [x] **T-319** — render: compose camera rotation into DETACHED canvas SO(3) bake · Owner: claude/T-319-propagate-canvas-rotation · PR: https://github.com/jakildev/IrredenEngine/pull/1087
- [x] **T-321** — engine/prefabs: extract AUTO_YAW_ROTATE as a reusable prefab system · Owner: claude/T-321-auto-yaw-rotate-prefab · PR: https://github.com/jakildev/IrredenEngine/pull/1082
- [x] **T-220** — perf: worker_threads axis + entity-count override in perf_grid_matrix · Owner: claude/T-220-worker-threads-perf-axis · PR: https://github.com/jakildev/IrredenEngine/pull/1081
- [x] **T-315** — perf: GPU-side clear for VOXEL_TO_TRIXEL_STAGE_1 canvas+distance textures · Owner: claude/T-315-gpu-side-canvas-clear · PR: https://github.com/jakildev/IrredenEngine/pull/1061
- [x] **T-314** — render: smooth sub-pixel camera at low game resolutions · Owner: claude/T-314-lowres-subpixel · PR: https://github.com/jakildev/IrredenEngine/pull/1066
- [x] **T-316** — render: skip Metal buffer orphan when GPU is idle · Owner: claude/T-316-metal-buffer-no-orphan · PR: https://github.com/jakildev/IrredenEngine/pull/1065
- [x] **T-317** — camera-rotation controls — canvas_stress auto-rotate + Ctrl+middle-drag rotate · Owner: claude/T-317-camera-rotation · PR: https://github.com/jakildev/IrredenEngine/pull/1063
- [x] **T-302** — retire C_Position3D / C_PositionGlobal3D / C_Rotation legacy components · Owner: claude/T-302-retire-legacy-position-rotation · PR: https://github.com/jakildev/IrredenEngine/pull/1062
- [x] **T-295** — DETACHED canvas SO(3) rotation · Owner: claude/t295-canvas-so3 · PR: https://github.com/jakildev/IrredenEngine/pull/1047
- [x] **T-311** — perf: CI baseline + automated regression gate for engine/render, engine/system, engine/math PRs · Owner: claude/T-311-ci-baseline-gate · PR: https://github.com/jakildev/IrredenEngine/pull/1039
- [x] **T-307** — skills: decompose /simplify into parallel reuse-detection subagents · Owner: claude/T-307-simplify-subagent-decomposition · PR: https://github.com/jakildev/IrredenEngine/pull/1040
- [x] **T-313** — perf: Lua-vs-C++ parity dashboard · Owner: claude/T-313-lua-cpp-parity-dashboard · PR: https://github.com/jakildev/IrredenEngine/pull/1037
