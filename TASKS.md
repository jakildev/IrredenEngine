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

- [ ] **lua-codegen: per-component emit shape to unlock PARALLEL_FOR** — convert codegen tool's system emit from batch form to per-component so CODEGEN systems can opt into PARALLEL_FOR
  - **ID:** T-347
  - **Area:** engine/script, engine/system
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** `lua_perf_grid` (CODEGEN, `concurrency = PARALLEL_FOR`) at 262k entities matches `perf_grid` (C++) within ±10%; existing CODEGEN tests (`lua_system_codegen_test.cpp`, `lua_system_coexistence_test.cpp`) pass against new emit shape; new CODEGEN PARALLEL_FOR test registers without FATAL and dispatches across workers
  - **Issue:** #1120
  - **Notes:** DSL parser in `cmake/lua_codegen/system_dsl.cpp` already recognizes canonical for-loop and `:at(i)` / `:setAt(i, ...)` ops; lowering is structural — recognize `local s = arch.C_Foo:at(i)` as row binding, drop outer for-statement. Gotcha: `std::vector<EntityId>& _ir_codegen_ids` slot may have callers — search before deleting; per-component form has id-aware overload. Filed by opus-worker during T-223.
  - **Links:**

- [ ] **engine/system: validator FATAL picks wrong rule on catch-all tick + PARALLEL_FOR** — order validator rules most-specific-first (or collapse to Form enum) so catch-all + PARALLEL_FOR emits the most-precise FATAL message
  - **ID:** T-349
  - **Area:** engine/system
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** a variadic catch-all tick `[](auto&&...) {}` with `PARALLEL_FOR` fires the relation-form FATAL (most-specific), not the entity-id FATAL; existing FATAL messages for real-world callers unchanged; `IrredenEngineTest` passes
  - **Issue:** #1125
  - **Notes:** Surfaced during Opus recheck of PR #1122 (T-334). Not a correctness issue — FATAL is still FATAL, only diagnostic precision. Three possible fixes: (1) order validator rules most-specific to least; (2) collapse `usesEntityId_`/`isBatchForm_`/`isRelationForm_` bits into a `Form` enum; (3) add precondition that at most one form bit is set for non-catch-all sigs. Linked: PR #1122.
  - **Links:**

- [ ] **engine/system: SERIAL fast-path + dual-slot consolidation** — drop redundant `functionTick_` for row-iterating forms; SERIAL/MAIN_THREAD calls binder directly without per-node heap allocation
  - **ID:** T-348
  - **Area:** engine/system
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** `IrredenEngineTest` + `IRShapeDebug` build clean on linux-debug; existing PARALLEL_FOR systems fan out correctly; `functionTick_` slot removed from row-iterating forms (only tag/relation-form `functionTick_` kept); no per-SERIAL-node binder allocation
  - **Issue:** #1124
  - **Notes:** Surfaced as T-333 (PR #1123) nit. Opus recheck on #1123 identified that `m_ticks[i].functionTick_` and `m_ticks[i].prepareRangedTick_` are aliased copies of the same closure (~64 bytes/system × N systems × M worlds). Fix: drop `functionTick_` for row-iterating forms; branch in `executeSystem` on `(prepareRangedTick_ != null && concurrency != PARALLEL_FOR)` → call `prepareRangedTick_(node)(0, length)` directly. Affected file: `engine/system/include/irreden/system/system_manager.hpp`.
  - **Links:**

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


- [~] **fleet: queue-manager queued/free divergence check** — add divergence check to role-queue-manager.md that flags mismatch between `fleet:queued` issue set and TASKS.md `free` rows; write warning to ~/.fleet/feedback/queue-manager.md
  - **ID:** T-342
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** claude/T-342-queue-manager-divergence-check
  - **Blocked by:** (none)
  - **Acceptance:** divergence check step added to role-queue-manager.md; any `fleet:queued` issue not matching a TASKS.md free row (or vice versa) written to feedback file; `queue-staleness` cluster quiet
  - **Issue:** #1135
  - **Notes:** Surfaced by review-fleet-feedback (signature: queue-staleness, 6 occurrences since 2026-05-02). `fleet:queued` label diverges from TASKS.md silently. Related to #1131 maintenance-sync; same proposed fix (fix-001 + divergence check).
  - **Links:**

- [ ] **lua-codegen: per-component emit shape for PARALLEL_FOR** — convert codegen system-emit from batch form to per-component so CODEGEN systems can opt into PARALLEL_FOR
  - **ID:** T-347
  - **Area:** engine/script, build
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** `lua_perf_grid` (CODEGEN, `concurrency = PARALLEL_FOR`) at 262k entities matches `perf_grid` (C++) within ±10%; existing CODEGEN tests (`lua_system_codegen_test.cpp`, `lua_system_coexistence_test.cpp`) pass against the new emit shape; new CODEGEN test exercising `concurrency = PARALLEL_FOR` registers without FATAL and dispatches across worker threads
  - **Issue:** #1120
  - **Notes:** DSL parser (`cmake/lua_codegen/system_dsl.cpp`) already recognizes canonical for-loop and `:at(i)` / `:setAt(i, ...)` column ops — lowering is structural: recognize `local s = arch.C_Foo:at(i)` as a row binding, `:setAt` as a row write, drop outer for-statement. Watch for existing callers of `std::vector<EntityId>& _ir_codegen_ids` before deleting. T-223 left `/* concurrency */ IRSystem::Concurrency::SERIAL` annotations on every emitted createSystem call — switching the default is mechanical once lowering lands. Filed by opus-worker during T-223.
  - **Links:**

- [ ] **engine/system: SERIAL fast-path + dual-slot consolidation** — drop `functionTick_` for row-iterating forms; SERIAL/MAIN_THREAD branches call binder directly, eliminating per-node allocation and dual-slot static overhead
  - **ID:** T-348
  - **Area:** engine/system
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** `functionTick_` removed for row-iterating forms; `executeSystem` branches on `(prepareRangedTick_ != null && concurrency != PARALLEL_FOR)` → calls `prepareRangedTick_(node)(0, length)` directly; tag/relation-form `functionTick_` slot kept; `IrredenEngineTest` + `IRShapeDebug` build clean; existing PARALLEL_FOR systems fan out correctly
  - **Issue:** #1124
  - **Notes:** Two-part scope: (1) SERIAL fast-path avoids per-node `std::function` heap allocation on non-PARALLEL_FOR path; (2) dual-slot consolidation drops ~64 bytes/system × N systems × M worlds of static `std::function` memory. Opus recheck on PR #1123 flagged that `m_ticks[i].functionTick_` and `m_ticks[i].prepareRangedTick_` are aliased copies. Affected file: `engine/system/include/irreden/system/system_manager.hpp`.
  - **Links:**

- [ ] **engine/system: validator FATAL picks wrong rule on variadic catch-all + PARALLEL_FOR** — order validator rules most-specific-first so the most useful diagnostic fires on ambiguous signatures
  - **ID:** T-349
  - **Area:** engine/system
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** with an artificial catch-all tick `[](auto&&...) {}` + `PARALLEL_FOR`, the FATAL message names the most-specific failing rule (relation-form > batch-form > entity-id); `IrredenEngineTest` passes; build clean
  - **Issue:** #1125
  - **Notes:** Not a correctness issue — FATAL is still FATAL. `validateConcurrencyForAccess` fires on the first rule in source order (currently `usesEntityId_`); a catch-all tick passes all three form probes. Fix options: order rules most-to-least-specific, collapse three form bits into a single `Form` enum, or add a precondition that at most one form bit is set. Linked: PR #1122 (T-334).
  - **Links:**

- [ ] **fleet/merger: re-target / rebase order on stacked-base merged path** — resolve the option-A/B trade-off and update role-merger.md (and possibly role-opus-worker.md) for correctness on the conflict branch
  - **ID:** T-350
  - **Area:** tooling
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** role-merger.md step a.5 ii updated with chosen option; if Option B, role-opus-worker.md step 1c updated to accept `fleet:semantic-conflict` PRs whose base was re-targeted to master; desk-check scenario (stacked PR with conflict after base merges) produces correct label + base state for opus-worker handoff
  - **Issue:** #1149
  - **Notes:** Two options: **Option A** — keep current re-target-first order, add doc comment explaining why (opus-worker rebases against baseRefName, so merger must set master first). **Option B** — invert (rebase first, re-target + cleanup only on clean exit); on conflict branch still remove `fleet:awaiting-base` / `fleet:stacked` / `fleet:needs-base-update` AND re-target to master so opus-worker rebases against the right tip. Key invariant: `fleet:awaiting-base` on a conflicted PR causes opus-worker step 1c to skip it indefinitely. Filed from PR #1146 review (sonnet reviewer nit).
  - **Links:**

- [ ] **platform-parity: IRPerfGrid ~1 FPS on linux-x86_64 — COMPUTE_LIGHT_VOLUME** — identify and fix the dominant lighting stage causing ~1 FPS on OpenGL/Mesa-d3d12
  - **ID:** T-351
  - **Area:** engine/render, shaders/glsl
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** `IRPerfGrid` auto-screenshot completes within 30s timeout on linux-x86_64 (OpenGL/Mesa-d3d12); PERF_STATS_OVERLAY confirms `COMPUTE_LIGHT_VOLUME` GPU time drops ≥8×; existing demos unaffected on macOS/Metal
  - **Issue:** #1154
  - **Notes:** Hot stage identified: `COMPUTE_LIGHT_VOLUME` — specifically `c_propagate_light_volume.glsl` × 32 iterations × 128³ cells ≈ 870M image ops + 800M SSBO reads per canvas per frame. Quick wins: (1) adaptive iteration count from per-light radius via `LightVolumeParams::stepFalloff` (placeholder was 32 flat); (2) skip COMPUTE_LIGHT_VOLUME dispatch for canvases with no active lights (GUI canvas). Larger: 64³ default volume (8× cheaper), sparse seeded-list propagate. Environment note: WSLg Mesa-d3d12 caps GL at 4.5 with below-native compute throughput — fix should work on native Linux too. Use `/optimize` flow + PERF_STATS_OVERLAY GPU timing to confirm dominant stage.
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-333** — engine/system: pre-resolve component vectors on main thread for PARALLEL_FOR · Owner: claude/T-333-pre-resolve-component-vectors · PR: https://github.com/jakildev/IrredenEngine/pull/1123
- [x] **T-334** — engine/system: validator rejects PARALLEL_FOR + relation-form · Owner: claude/T-334-parallel-for-relation-form-validator · PR: https://github.com/jakildev/IrredenEngine/pull/1122
- [x] **T-225** — entity: thread-safe deferred mutations from workers · Owner: claude/T-225-parallel-spawn · PR: https://github.com/jakildev/IrredenEngine/pull/1109
- [x] **T-344** — fleet/auto-mode: fix rm -f .review-body.md via Read-then-Write protocol · Owner: claude/T-344-auto-mode-allowlist · PR: https://github.com/jakildev/IrredenEngine/pull/1151
- [x] **T-343** — fleet: review-pr live label check after claim acquisition (pre-checkout) · Owner: claude/T-343-review-pr-live-label-check · PR: https://github.com/jakildev/IrredenEngine/pull/1150
- [x] **T-346** — fleet: scout stackable_blocker_pr false-positive filter · Owner: claude/T-346-scout-stackable-filter · PR: https://github.com/jakildev/IrredenEngine/pull/1147
- [x] **T-340** — fleet/merger: rebase + verdict preservation on merged-base re-target · Owner: claude/T-340-merger-merged-base-retarget · PR: https://github.com/jakildev/IrredenEngine/pull/1146
- [x] **T-345** — fleet: fleet-build --target format restricted to touched files · Owner: claude/T-345-fleet-build-format-touched-files · PR: https://github.com/jakildev/IrredenEngine/pull/1145
- [x] **T-339** — fleet: review-pr verdict-label retry-and-verify guard · Owner: claude/T-339-review-pr-verdict-label-retry · PR: https://github.com/jakildev/IrredenEngine/pull/1144
- [x] **T-337** — Tools: shell autocomplete for ir-build and ir-run targets · Owner: claude/T-337-shell-autocomplete · PR: https://github.com/jakildev/IrredenEngine/pull/1129
- [x] **T-223** — lua: concurrency field on IRSystem.registerSystem (EVAL + CODEGEN paths) · Owner: claude/T-223-lua-concurrency · PR: https://github.com/jakildev/IrredenEngine/pull/1121
- [x] **T-332** — demos: perf_grid UPDATE pipeline parallel group · Owner: claude/T-332-update-pipeline-groups · PR: https://github.com/jakildev/IrredenEngine/pull/1117
- [x] **T-336** — investigate + fix macOS demo segfault on shutdown · Owner: claude/T-336-macos-shutdown · PR: https://github.com/jakildev/IrredenEngine/pull/1118
- [x] **T-328** — system: complete T-222 POC ports + SystemAccess tag-shadow fix · Owner: claude/T-328-system-poc-ports-systemaccess-fix · PR: https://github.com/jakildev/IrredenEngine/pull/1112
- [x] **T-335** — test/system: PARALLEL_FOR dispatch integration tests · Owner: claude/T-335-parallel-dispatch-test · PR: https://github.com/jakildev/IrredenEngine/pull/1110
- [x] **T-224** — system: pipeline groups + cross-system access validation · Owner: claude/T-224-pipeline-groups · PR: https://github.com/jakildev/IrredenEngine/pull/1104
- [x] **T-330** — tools: ir-perf-grid + fingerprinted baselines (sub-task 3 of #1074) · Owner: claude/T-330-ir-perf-grid · PR: https://github.com/jakildev/IrredenEngine/pull/1115
- [x] **T-331** — docs: acquire-late, release-early lock rule in worker-role docs · Owner: claude/T-331-acquire-late-release-early-docs · PR: https://github.com/jakildev/IrredenEngine/pull/1113
- [x] **T-222** — system: Concurrency::PARALLEL_FOR + single-system access validation · Owner: claude/T-222-parallel-for-validation · PR: https://github.com/jakildev/IrredenEngine/pull/1097
- [x] **T-329** — tools: ir-build / ir-run wrappers with ir-acquire wiring · Owner: claude/T-329-ir-build-run · PR: https://github.com/jakildev/IrredenEngine/pull/1111
