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


- [~] **Script: Lua codegen + EVAL concurrency integration** — extend Lua DSL with concurrency field; CODEGEN maps to Concurrency enum; EVAL mode overrides to MAIN_THREAD with warning
  - **ID:** T-223
  - **Area:** engine/script, creations/demos/lua_perf_grid
  - **Model:** opus
  - **Owner:** claude/T-223-lua-concurrency
  - **Blocked by:** (none)
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
  - **Blocked by:** (none)
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


- [ ] **render: retire SCREEN_SPACE_RESIDUAL_ROTATE passthrough stage** — delete the passthrough system, dedicated shader pair, and UBO struct; replace every consumer with FRAMEBUFFER_TO_SCREEN
  - **ID:** T-323
  - **Area:** engine/render, engine/prefabs/irreden/render, shaders/glsl, shaders/metal
  - **Model:** sonnet
  - **Owner:** free
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
  - **Blocked by:** (none)
  - **Acceptance:** randomFloat/randomBool/randomInt route through IRJobs::workerRng() (unit-tested); ANIMATION_COLOR static caches replaced with member fields on registerSystem form; VELOCITY_DRAG + ANIMATION_COLOR opt in to PARALLEL_FOR where safe; deriveAccessFromSignature correctly handles tag-bearing packs via TypeList filter; T-222 validator workaround in system_concurrency_test.cpp replaced with proper deriveAccessFromSignature call; const C_Foo dispatch path covered by unit test
  - **Issue:** #1096
  - **Notes:** Deferred from T-222 (PR #1097). Sub-tasks: A) route rand()-based IRMath::randomFloat/randomBool/randomInt through per-worker IRJobs::workerRng() mt19937; B) ANIMATION_COLOR: move colorTrackCache/clipCache to member fields, convert to registerSystem + tick() form; C) VELOCITY_DRAG + ANIMATION_COLOR PARALLEL_FOR opt-in after A+B (replace std::sin/std::abs with IRMath in VELOCITY_DRAG diff); D) ~30 LOC TypeList filter to partition tag types out of component pack before invocability probes; E) unit test exposing const C_Foo dispatch UB (see issue comment from jakildev). Lands before T-223.
  - **Links:**


- [~] **System: migrate UPDATE pipeline to multi-system groups + measure perf_grid_matrix speedup (T-224 follow-up)** — const-correctness audit of prefab systems to unlock real parallel groups; run perf matrix and file speedup report
  - **ID:** T-332
  - **Area:** engine/system, creations/demos
  - **Model:** opus
  - **Owner:** claude/T-332-update-pipeline-groups
  - **Blocked by:** T-224
  - **Acceptance:** at least one demo's UPDATE pipeline has a real parallel group accepted by the T-224 validator; perf_grid_matrix.sh shows additional speedup beyond T-222's baseline; speedup number filed to docs/perf-reports/threading_phase3.md; no regressions on existing demos
  - **Issue:** #1103
  - **Notes:** Deferred from T-224 (infrastructure landed; migration + measurement deferred). Gating work: most prefab systems haven't declared const opt-in on tick params; validator conservatively classifies all reads as writes and rejects every multi-system grouping. Per-prefab const-correctness audit is required first. Part of epic #226.
  - **Links:**


- [~] **System: pre-resolve component-vector refs on main thread before PARALLEL_FOR dispatch** — eliminate operator[] race on EntityManager from worker threads; pre-bind component vectors before parallelFor call
  - **ID:** T-333
  - **Area:** engine/system, engine/entity
  - **Model:** opus
  - **Owner:** claude/T-333-pre-resolve-component-vectors
  - **Blocked by:** (none)
  - **Acceptance:** worker bodies under PARALLEL_FOR no longer call m_pureComponentTypes::operator[]; ThreadSanitizer-clean parallelFor dispatch over archetype with >=kDefaultGrainSize entities; VELOCITY_3D continues to tick correctly
  - **Issue:** #1105
  - **Notes:** Deferred from T-222 review (PR #1097, Opus recheck nit #1). Latent UB: getComponentData<C>(node) calls m_pureComponentTypes[typeName] (operator[], non-const) from each worker. Works today on libstdc++/libc++/MSVC by coincidence; real race as PARALLEL_FOR broadens (T-223, T-225). Cleanest fix: pre-resolve component vectors in SystemManager::executeSystem before IRJobs::parallelFor, pass as std::tuple into rangedFn. Smaller fix: switch to m_pureComponentTypes.at(typeName). Part of epic #226. Should land before T-223.
  - **Links:**


- [~] **System: PARALLEL_FOR + relation-form validator gap** — add validator rule rejecting PARALLEL_FOR systems using relation-form tick; add SystemAccess::isRelationForm_ bit
  - **ID:** T-334
  - **Area:** engine/system
  - **Model:** opus
  - **Owner:** claude/T-334-parallel-for-relation-form-validator
  - **Blocked by:** (none)
  - **Acceptance:** validateConcurrencyForAccess rejects PARALLEL_FOR + relation-form at registration time; unit test in system_concurrency_test.cpp confirms rejection (mirrors BatchFormRejected shape); existing relation-form systems (all currently SERIAL) tick unchanged
  - **Issue:** #1106
  - **Notes:** Deferred from T-222 review (PR #1097, Opus recheck nit #2). rangedFn's relation branch calls getRelatedEntityFromArchetype + getComponentOptional<RelComps> — both EntityManager hash-map lookups that race under PARALLEL_FOR. Not a real bug today (no system combines relations + PARALLEL_FOR) but must land before T-223 broadens rollout. Preferred fix: add isRelationForm_ bit to SystemAccess (set from relation-form trait branch), add validator rule. Part of epic #226.
  - **Links:**


- [~] **Test/System: integration test that PARALLEL_FOR dispatch parallelizes and processes every row** — fixture ticks PARALLEL_FOR system over >=kDefaultGrainSize entities, asserts every entity processed exactly once
  - **ID:** T-335
  - **Area:** engine/system, test/system
  - **Model:** sonnet
  - **Owner:** claude/T-335-parallel-dispatch-test
  - **Blocked by:** (none)
  - **Acceptance:** new test in test/system/ ticks PARALLEL_FOR system over >=kDefaultGrainSize entities and asserts every row processed exactly once; TSAN-friendly variant using vector<atomic<int>> catches worker overlap; existing 894 tests pass; optional: test confirms PARALLEL_FOR + relation-form rejected at registration (requires T-334)
  - **Issue:** #1107
  - **Notes:** Deferred from T-222 review (PR #1097, Opus recheck nit #3). Core test: spin up EntityManager + SystemManager + JobManager(2), register PARALLEL_FOR system with atomic counter tick body, populate 4096 entities (forces multiple kDefaultGrainSize=512 chunks), tick once, assert counter==4096. Optional relation-form rejection test depends on T-334 landing first. Part of epic #226.
  - **Links:**


- [~] **Investigate + fix macOS demo segfault/non-clean shutdown** — reproduce the shutdown crash on macOS (e.g. IRPerfGrid); identify root cause; apply targeted fix; harden run/verify tooling to validate exit codes
  - **ID:** T-336
  - **Area:** engine/render, creations/demos
  - **Model:** opus
  - **Owner:** claude/T-336-macos-shutdown
  - **Blocked by:** (none)
  - **Acceptance:** IRPerfGrid and at least two other representative demos exit cleanly on macOS (exit code 0, no crash reporter dialog); fix verified on macos-debug preset; fleet-build / ir-run wrapper does not mask the non-zero exit code; no regression on linux-debug
  - **Issue:** #1116
  - **Notes:** Reported on macOS; IRPerfGrid cited as one reproducer. Root cause unknown — may involve Metal resource teardown order, ECS world destructor sequencing, or missing signal handler. Also investigate whether run/verify skills should assert clean exit code after demo auto-screenshot runs.
  - **Links:**

- [~] **Tools: shell autocomplete for ir-build and ir-run targets** — add bash/zsh tab-completion for `--target` on `ir-build` and positional target on `ir-run`; completion discovers available targets from the current repo's build tree so game-repo invocations show game targets
  - **ID:** T-337
  - **Area:** tooling, engine/tools
  - **Model:** sonnet
  - **Owner:** claude/T-337-shell-autocomplete
  - **Blocked by:** (none)
  - **Acceptance:** `ir-build --target <TAB>` and `ir-run <TAB>` complete available cmake targets in bash and zsh; completion script installed/registered via engine/tools setup (or documented in BUILD.md); when invoked from game-repo build root, game targets appear; no regression on existing ir-build / ir-run behaviour
  - **Issue:** #1127
  - **Notes:** Issue says "same as fleet-run used to have" — check if a completion snippet exists for fleet-run to reuse. Also requested: --target flag completion on ir-build (not just ir-run). Game-targets: parameterize by CWD build root (cmake --build . --target help or equivalent) so one script serves both repos.
  - **Links:**


- [ ] **fleet: queue-manager maintenance-sync — re-derive TASKS.md from issue bodies on every wake** — add maintenance-sync step to role-queue-manager.md that re-derives stale rows from issue body and PR-merge state before any other step
  - **ID:** T-338
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** maintenance-sync step added to role-queue-manager.md; fleet feedback `stale-task-row` cluster does not re-surface in subsequent digest runs
  - **Issue:** #1131
  - **Notes:** Surfaced by review-fleet-feedback (signature: stale-task-row, 50 occurrences since 2026-05-01). Workers claim rows for closed-completed tasks; queue-manager re-opened merged T-104 without flipping to [x]. Proposed fix: add maintenance-sync step at top of role-queue-manager.md startup.
  - **Links:**


- [ ] **fleet: review-pr verdict-label retry-and-verify guard** — split combined `gh pr edit --remove/--add` into two calls; wrap add in retry-and-verify; cross-ref role-sonnet-reviewer and role-opus-reviewer
  - **ID:** T-339
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** verdict-label step in review-pr/SKILL.md splits remove/add into two `gh` calls; add wrapped in retry-and-verify (re-query labels, fail loud if verdict label still missing); fleet feedback `label-absent-after-verdict` cluster quiet
  - **Issue:** #1132
  - **Notes:** Surfaced by review-fleet-feedback (signature: label-absent-after-verdict, 37 occurrences since 2026-04-30). fleet:approved/fleet:needs-fix labels silently missing after verdict; downstream PRs stall. Split combined `gh pr edit`, add retry-and-verify.
  - **Links:**


- [ ] **fleet: merger stacked-PR merged-base re-target with rebase path** — add detect-base-in-master → re-target to master → rebase branch path between steps 2.5 and 2.6 in role-merger.md; preserve verdict labels across swap
  - **ID:** T-340
  - **Area:** tooling
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** merger detects stacked PR whose base was merged to master; re-targets to master; rebases branch; preserves any live fleet:needs-fix / fleet:approved label; `stacked-pr-merger-gap` cluster quiet
  - **Issue:** #1133
  - **Notes:** Surfaced by review-fleet-feedback (signature: stacked-pr-merger-gap, 8 occurrences since 2026-04-30). Merger left rebase state in worktree; MERGEABLE stacked PRs fell through every step; verdict labels silently dropped. Add path between steps 2.5–2.6 in role-merger.md.
  - **Links:**


- [ ] **fleet: fleet-worktree-busy-branches live re-derive from git worktree list** — remove caching from fleet-worktree-busy-branches; re-derive from live `git worktree list --porcelain` on every call; add --repo loop for game repo
  - **ID:** T-341
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** fleet-worktree-busy-branches uses live `git worktree list --porcelain` with no cache; `--repo <path>` covers both repos; `worktree-tracker-drift` cluster quiet
  - **Issue:** #1134
  - **Notes:** Surfaced by review-fleet-feedback (signature: worktree-tracker-drift, 7 occurrences since 2026-04-30). Stale data caused checkout race (workers blocked on branches already abandoned). Also add fallback verify-after-checkout in role-sonnet-author.md and role-opus-worker.md per issue.
  - **Links:**


- [ ] **fleet: queue-manager queued/free divergence check** — add divergence check to role-queue-manager.md that flags mismatch between `fleet:queued` issue set and TASKS.md `free` rows; write warning to ~/.fleet/feedback/queue-manager.md
  - **ID:** T-342
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** divergence check step added to role-queue-manager.md; any `fleet:queued` issue not matching a TASKS.md free row (or vice versa) written to feedback file; `queue-staleness` cluster quiet
  - **Issue:** #1135
  - **Notes:** Surfaced by review-fleet-feedback (signature: queue-staleness, 6 occurrences since 2026-05-02). `fleet:queued` label diverges from TASKS.md silently. Related to #1131 maintenance-sync; same proposed fix (fix-001 + divergence check).
  - **Links:**


- [ ] **fleet: review-pr live label check after claim acquisition (pre-checkout)** — add `gh pr view <N> --json labels` live-check immediately after acquiring fleet:reviewing-* claim, before checkout; bail if fleet:semantic-conflict/fleet:merger-cooldown/fleet:wip appeared since cache snapshot
  - **ID:** T-343
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** live label check added to review-pr/SKILL.md after claim acquisition; bail on fleet:semantic-conflict, fleet:merger-cooldown, fleet:wip; `state-cache-lag` cluster quiet
  - **Issue:** #1136
  - **Notes:** Surfaced by review-fleet-feedback (signature: state-cache-lag, 5 occurrences since 2026-05-06). Reviewer picks PR via cache, conflict label appears between snapshot and checkout; reviewer wastes checkout + diff read then releases without review. Cost: one extra `gh` call per candidate.
  - **Links:**


- [ ] **fleet: extend auto-mode classifier allow-list for fleet-role workflows** — whitelist role-*.md / .claude/commands/role-*.md writes from fleet roles; allow `rm -f .review-body.md` and prescribed `bash -c kill` exit patterns
  - **ID:** T-344
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** auto-mode classifier allows fleet roles to write role-*.md docs, delete .review-body.md, and issue prescribed kill-based exit; `permission-gate-friction` cluster quiet
  - **Issue:** #1137
  - **Notes:** Surfaced by review-fleet-feedback (signature: permission-gate-friction, 5 occurrences since 2026-04-30). `rm` blocked on .fleet/plans/; kill exit blocked; role-*.md Self-Modification gate fires on role docs. Proposed fix: extend .claude/settings.json allow-list.
  - **Links:**


- [ ] **fleet: fleet-build --target format restricted to touched files** — in scripts/fleet/fleet-build, restrict `--target format` to files from `git diff --name-only` against merge-base (*.cpp *.hpp) only
  - **ID:** T-345
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** `fleet-build --target format` runs clang-format only on files changed since merge-base; no full-tree reformat; `format-target-overreach` cluster quiet
  - **Issue:** #1138
  - **Notes:** Surfaced by review-fleet-feedback (signature: format-target-overreach, 3 occurrences since 2026-05-06). `fleet-build --target format` reformats entire engine (458 files), risking unrelated formatter drift in feature PRs. Restrict to `git diff --name-only $(git merge-base HEAD origin/master) -- *.cpp *.hpp`.
  - **Links:**


- [ ] **fleet: scout stackable_blocker_pr false-positive filter** — in fleet-state-scout, add filter: (a) verify downstream task files not already inside blocker PR diff; (b) drop candidates with fleet:design-blocked or fleet:design-escalated
  - **ID:** T-346
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** stackable filter drops candidates whose downstream files are inside blocker PR diff or whose issue has fleet:design-blocked/fleet:design-escalated; `stackable-false-positive` cluster quiet
  - **Issue:** #1139
  - **Notes:** Surfaced by review-fleet-feedback (signature: stackable-false-positive, 3 occurrences since 2026-05-12). Variants: blocker PR already contains downstream task files (T-299 variant); prior filters miss fleet:design-blocked issues.
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-330** — tools: ir-perf-grid + fingerprinted baselines (sub-task 3 of #1074) · Owner: claude/T-330-ir-perf-grid · PR: https://github.com/jakildev/IrredenEngine/pull/1115
- [x] **T-331** — docs: acquire-late, release-early lock rule in worker-role docs · Owner: claude/T-331-acquire-late-release-early-docs · PR: https://github.com/jakildev/IrredenEngine/pull/1113
- [x] **T-222** — system: Concurrency::PARALLEL_FOR + single-system access validation · Owner: claude/T-222-parallel-for-validation · PR: https://github.com/jakildev/IrredenEngine/pull/1097
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
