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

- [~] **Render: final occupancy-grid teardown (drop BUILD_OCCUPANCY_GRID + C_OccupancyGrid)** — pure deletion after T-091 (AO) and T-072 (light-volume) land; remove grid system, component, SSBO, constants, and CLAUDE.md phased-out sections
  - **ID:** T-092
  - **Area:** engine/render, engine/prefabs/irreden/render, shaders/glsl, shaders/metal
  - **Model:** sonnet
  - **Owner:** claude/T-092-occupancy-grid-teardown
  - **Blocked by:** T-126
  - **Acceptance:** (1) `grep -rn 'C_OccupancyGrid\|OccupancyGrid\|kBufferIndex_OccupancyGrid\|BUILD_OCCUPANCY_GRID\|occupancyGetBit'` returns zero hits across `engine/`, `creations/`, `test/`; (2) all lighting demos (`IRLightingCombined`, `IRLightingSunShadow`, `IRLightingEmissive`, `IRLightingPoint`, `IRLightingSpot`, `IRShapeDebug`) render identically to pre-deletion reference via `render-debug-loop`; (3) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`; (4) CLAUDE.md phased-out sections from T-071 removed; one-line note added pointing to the PR that retired the grid
  - **Issue:** #429
  - **Notes:** Zero-design task — delete only, no new behavior. Trivial PR once T-072 consumers are gone. If a hidden consumer is found, bounce it upstream to T-072 rather than partially deleting. Also: promote `kBufferIndex_SunShadowDepthMap = 28` as canonical slot-28 name (retiring the alias); delete CPU↔GPU `roundHalfUp` parity contract docs if no other consumer depends on it (verify light-volume GPU port first). CAUTION: T-091 (issue #428) was manually closed without a merged PR — the AO migration via trixelDistances was NOT completed; verify that `c_compute_voxel_ao` still does not read `OccupancyGridBuffer` before starting this deletion, or re-file the AO migration work first.
  - **Links:**


- [~] **Lua-driven ECS: pipeline composition + enum bindings + modifier-framework bindings** — bind IRSystem::registerPipeline, SystemName/GameSystemName enums, and modifier framework (C_Modifiers, transforms, C_ResolvedFields, field-binding registry) to Lua; demo creation with pure-Lua initSystems
  - **ID:** T-102
  - **Area:** engine/script, engine/system
  - **Model:** opus
  - **Owner:** claude/T-102-lua-pipeline-modifier-bindings
  - **Blocked by:** (none)
  - **Acceptance:** (1) Lua calls IRSystem.registerPipeline(IRTime.UPDATE, { IRSystem.systemId(SystemName.LIFETIME), luaSystemId, IRGameSystem.systemId(GameSystemName.GRID_BAKE) }) and all three execute in declared order; (2) IRModifier.add(entity, "Hp.current", { transform = MULTIPLY, value = 0.5 }) against Lua-defined component reflects in C_ResolvedFields; (3) sample demo creation whose entire initSystems lives in main.lua (mixing engine, game, and Lua systems) runs without crash; (4) fleet-build clean on linux-debug
  - **Issue:** #490
  - **Notes:** PR 4 of 6 for parent epic #293. Full architect plan in .fleet/plans/T-102.md. Blocked by T-101 (Lua systems). New file: engine/script/include/irreden/script/lua_modifier_bindings.hpp. Convenience: IRSystem.systemId(SystemName.X) -> SystemId.
  - **Links:**

- [ ] **Lua-driven ECS: hot-reload of Lua system bodies** — add IRSystem::replaceSystemBody(systemId, newFn) C++ + Lua binding; rebinds sol::function in place with no archetype changes or entity migration; document in engine/script/CLAUDE.md
  - **ID:** T-103
  - **Area:** engine/script, engine/system
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-102
  - **Acceptance:** (1) change a math constant in a Regen tick body in main.lua; call IRSystem.replaceSystemBody(regenSystemId, newFn); observe next-tick behavior change without process restart; (2) SystemId unchanged across the body swap; (3) in-flight entities use new body on next tick with no special handling; (4) doc in engine/script/CLAUDE.md alongside existing trait-based binding pattern; (5) fleet-build clean on linux-debug
  - **Issue:** #491
  - **Notes:** PR 5 of 6 for parent epic #293. Full architect plan in .fleet/plans/T-103.md. Blocked by T-102 (pipeline + modifier bindings). Component-schema hot-reload is explicitly out of scope (follow-up in docs/design/lua-driven-ecs.md).
  - **Links:**

- [ ] **Lua-driven ECS: Lua port of perf_grid + perf parity gate** — new demo creations/demos/lua_perf_grid/ mirroring perf_grid (262k entities, wave animation, same render pipeline) entirely in Lua; parity gate: Lua wave-animation per-tick cost <= 1.5x C++ equivalent
  - **ID:** T-104
  - **Area:** engine/script, creations/demos/lua_perf_grid
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-103
  - **Acceptance:** (1) fleet-build --target IRLuaPerfGrid clean on linux-debug; (2) fleet-run IRLuaPerfGrid runs without crash (64x64x64 voxel grid, wave animation, same render pipeline as perf_grid); (3) parity gate: Lua wave-animation system per-tick cost <= 1.5x C++ SystemPeriodicIdlePositionOffset per-tick cost measured via IRProfile with profiling_enabled=true; (4) measured ratio documented in docs/design/lua-driven-ecs.md retrospective; (5) if gate fails: design doc PR amended with corrective decision before further work
  - **Issue:** #492
  - **Notes:** PR 6 of 6 for parent epic #293 — formal acceptance gate for the entire Lua-driven ECS stack. Full architect plan in .fleet/plans/T-104.md. Blocked by T-103 (hot-reload). If parity gate fails, this PR does not merge; instead amend T-099's design doc with corrective decision (LuaJIT migration, codegen-bound bodies, etc.).
  - **Links:**



- [~] **Lua-driven ECS: field index + index-style accessors for zero-string hot path** — expose field column index in registration handle; add `IREntity.getLuaField`/`setLuaField` for zero-string per-tick access; document two-tier accessor contract in engine/script/CLAUDE.md
  - **ID:** T-109
  - **Area:** engine/script, engine/entity
  - **Model:** sonnet
  - **Owner:** claude/T-109-lua-field-index-accessors
  - **Blocked by:** (none)
  - **Acceptance:** (1) `IRComponent.register` per-field handle carries `index`; (2) `IREntity.getLuaField`/`setLuaField` work by field index with no string lookup or table allocation; (3) out-of-range `fieldIndex` raises Lua error naming the offending index; (4) table-style `addLuaComponent`/`getLuaComponent` unchanged; (5) tests cover index round-trip, out-of-range error, and table-style regression; (6) `fleet-build --target IrredenEngineTest` clean on `linux-debug` and `macos-debug`; (7) `engine/script/CLAUDE.md` two-tier accessor section added
  - **Issue:** #514
  - **Notes:** Follow-up to T-100 (PR #508). Additive — no changes to existing table-style API. Enables Lua systems to cache `field.index` once at script load and call `getLuaField`/`setLuaField` per tick with zero string work. Unblocks T-101 to commit to a zero-string per-tick contract. Key files: `engine/script/src/lua_script.cpp` (add field.index + getLuaField/setLuaField bindings), `engine/script/include/irreden/script/i_component_data_lua_typed.hpp` (add readFieldAt/writeFieldAt), `test/script/lua_component_register_test.cpp` (index accessor tests).
  - **Links:**

- [~] **Worker role docs: stackable-blocked fallback pickup tier** — update sonnet-author and opus-worker step 3 with two-tier task pickup: unblocked first, stackable-blocked only if no unblocked tasks exist
  - **ID:** T-112
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** claude/T-112-stackable-blocked-pickup
  - **Blocked by:** (none)
  - **Acceptance:** (1) role-sonnet-author.md and role-opus-worker.md step 3 describe two-tier pickup with only-if-no-unblocked ordering; (2) stacked PR opens with --base $base --label fleet:stacked via commit-and-push cursor-stack mode; (3) multi-blocker tasks (Blocked by: T-A, T-B) explicitly excluded from fallback tier; (4) engine-only guard: game-side stackable tasks not picked up in v1; (5) fleet-claim claim --stackable-on invocation described in worker steps
  - **Issue:** (none)
  - **Notes:** PR 3 of 6 for #501. Full architect plan in .fleet/plans/T-110.md. Must land after T-110 and T-111. Key files: .claude/commands/role-sonnet-author.md, .claude/commands/role-opus-worker.md.
  - **Links:**

- [ ] **Docs: cross-author stacking lifecycle in FLEET.md** — new "Cross-author stacking (scheduler)" subsection covering full lifecycle (claim → PR open → reviewer gate → upstream feedback rebase → upstream merge re-target), Q1/Q2/Q3 decisions, and v1 limitations
  - **ID:** T-115
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-112
  - **Acceptance:** (1) docs/agents/FLEET.md has "Cross-author stacking (scheduler)" section with full lifecycle walkthrough; (2) Q1 (only-if-no-unblocked), Q2 (merger-driven hybrid rebase), Q3 (single-blocker-only v1) decisions documented; (3) v1 limitations listed (engine-only, single-blocker, multi-blocker not eligible); (4) pointer to fleet-claim and fleet-state-scout for implementation detail; (5) no other docs changed
  - **Issue:** #501
  - **Notes:** PR 6 of 6 for #501 — lands last, closes the tracking issue. Full architect plan in .fleet/plans/T-110.md.
  - **Links:**

- [~] **Render: per-canvas light scope via CHILD_OF relation** — scope C_LightSource to a specific canvas; lights become children of their target canvas via CHILD_OF; lighting systems iterate per-canvas through existing RelationParams machinery
  - **ID:** T-116
  - **Area:** engine/render, engine/prefabs/irreden/render
  - **Model:** opus
  - **Owner:** claude/T-116-per-canvas-light-scope
  - **Blocked by:** (none)
  - **Acceptance:** (1) two-canvas demo where a C_LightSource parented via CHILD_OF to canvas A produces no lighting contribution in canvas B; (2) existing single-canvas lighting demos (IRLightingCombined, IRLightingPoint, IRLightingSpot) unaffected; (3) fleet-build clean on linux-debug AND macos-debug
  - **Issue:** #363
  - **Notes:** Follow-up from lighting-fidelity-polish PR (audit finding #11). Preferred implementation: Option B (CHILD_OF relation) over Option A (explicit scope tag) — composes with existing RelationParams machinery and falls out of existing system patterns. Every C_LightSource currently contributes to every canvas with a C_CanvasLightVolume; scoping is useful for UI/inset canvases that should not receive world-space lights.
  - **Links:**

- [ ] **Render: HDR pipeline — RGBA16F canvas, tonemap pass, exposure control, sky term** — grow LDR pipeline into HDR; RGBA16F canvas color attachment; tonemap pass between LIGHTING_TO_TRIXEL and TRIXEL_TO_FRAMEBUFFER; exposure uniform; additive sky-term from emissive top hemisphere
  - **ID:** T-118
  - **Area:** engine/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) bright emissive lights no longer clip at white; saturation preserved through lighting → tonemap chain; (2) new lighting demo (IRLightingHDR or similar) exercises full HDR pipeline; (3) existing lighting demos (IRLightingCombined, IRLightingPoint, IRLightingSpot, IRLightingEmissive, IRLightingSunShadow) look identical to pre-HDR LDR output at default exposure; (4) fleet-build clean on linux-debug AND macos-debug
  - **Issue:** #366
  - **Notes:** Follow-up from lighting-fidelity-polish PR (audit findings #35-#38). Not in the lighting-fidelity-polish PR because HDR is a separate correctness dimension requiring its own tonemap tuning, demo screenshots, and perf measurement. Pick one tonemap operator and ship it (Reinhard, ACES, or Uncharted-2). Sky term: emissive top hemisphere driving additive contribution that cuts off at occlusion — cheap and visually impactful.
  - **Links:**


- [~] **Fleet: per-role concurrency cap config + dispatcher enforcement** — fleet-up.conf per-role concurrency field; dispatcher defers dispatch when in-flight+reserved >= cap[role]; default opus-worker=2, sonnet-fleet=4
  - **ID:** T-125
  - **Area:** tooling
  - **Model:** opus
  - **Owner:** claude/T-125-fleet-concurrency-cap
  - **Blocked by:** (none)
  - **Stack:** T-120..T-125 worktree-reservations
  - **Acceptance:** (1) fleet-up.conf gains per-role concurrency config (opus-worker: concurrency=2, sonnet-fleet: concurrency=4); (2) dispatcher defers when in-flight+reserved for role >= cap; (3) with concurrency=2, third concurrent opus-worker iteration deferred even with free worktrees; (4) env-overridable; (5) per-pane parallelism preserved for roles under cap; (6) default values preserve current behavior
  - **Issue:** (none)
  - **Notes:** PR 6 of 6 for #521, parallelizable with T-123 after T-122. Full plan in .fleet/plans/T-120.md. Files: ~/bin/fleet-up (config schema), ~/bin/fleet-dispatcher (cap-check at dispatch time). Soft cap — orthogonal to reservations; reservations protect dirty-state continuity, cap protects credit budget.
  - **Links:**

- [~] **Render: migrate light-volume propagation off CPU-built OccupancyGrid SSBO** — remove OccupancyGrid SSBO reads from the propagation system and shaders, replacing with a decoupled SSBO producer; unblocks T-092 deletion pass
  - **ID:** T-126
  - **Area:** engine/render, engine/prefabs/irreden/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** claude/T-126-occupancy-ssbo-decouple
  - **Blocked by:** (none)
  - **Acceptance:** (1) c_propagate_light_volume.glsl and .metal contain zero reads of OccupancyGridBuffer/occupancyGetBit; (2) system_compute_light_volume.hpp archetype no longer includes C_OccupancyGrid; (3) all lighting demos (IRLightingCombined, IRLightingPoint, IRLightingSpot, IRLightingEmissive, IRLightingSunShadow) render correctly; (4) fleet-build clean on linux-debug AND macos-debug; (5) T-092's hidden-consumer blocker is resolved
  - **Issue:** #532
  - **Notes:** Path 1 (standalone bitfield producer, recommended): move SSBO production out of C_OccupancyGrid lifecycle; bit-packing and camera-anchor math already in system_build_occupancy_grid.hpp. Path 2 (trixelDistances LOS) is the long-term direction per engine/render/CLAUDE.md but higher complexity — out of scope for this targeted unblock. Key files: system_compute_light_volume.hpp (remove C_OccupancyGrid from archetype, use standalone produced SSBO), c_propagate_light_volume.glsl and .metal (remove OccupancyGrid binding), creations demo callsites (remove C_OccupancyGrid setComponent). Also covers findings from issues #524 and #530 (parallel escalations of the same hidden consumer). T-117 (PR #522, SDF occlusion) added a second bitfield region (lightBlockerGetBit) to the same SSBO — if #522 is already merged, include that region in the migration too.
  - **Links:**

---

## In progress

<!-- Tasks currently being worked on. Mirror of [~] items above. -->


---

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-124** — Fleet: stuck-worktree staleness escalation · Owner: claude/T-124-stuck-worktree-escalation · PR: https://github.com/jakildev/IrredenEngine/pull/542
- [x] **T-123** — Fleet: worktree naming migration (opus-worker-N → worktree-N) · Owner: claude/T-123-fleet-up-boot-reconciliation · PR: https://github.com/jakildev/IrredenEngine/pull/540
- [x] **T-121** — Fleet: dispatcher reservation-aware pane selection · Owner: claude/T-121-auto-reserve-on-claim · PR: https://github.com/jakildev/IrredenEngine/pull/538
- [x] **T-113** — Merger: cascade rebase on upstream force-push for stacked PRs · Owner: claude/T-113-merger-cascade-rebase · PR: https://github.com/jakildev/IrredenEngine/pull/537
- [x] **T-111** — Scout: pre-compute stackable_blocker_pr field · Owner: claude/T-111-scout-stackable-blocker-pr · PR: https://github.com/jakildev/IrredenEngine/pull/536
- [x] **T-122** — Fleet: role docs startup reservation check · Owner: claude/T-122-role-startup-reservation · PR: https://github.com/jakildev/IrredenEngine/pull/533
- [x] **T-098** — Sprite: Lua bindings + sprite_demo creation · Owner: claude/T-098-sprite-lua-demo · PR: https://github.com/jakildev/IrredenEngine/pull/527
- [x] **T-117** — Render: SDF occlusion in point/spot light line-of-sight · Owner: claude/T-117-sdf-occlusion-light-los · PR: https://github.com/jakildev/IrredenEngine/pull/522
- [x] **T-119** — Fleet: usage-limit back-off for fleet-dispatcher transient workers · Owner: claude/T-119-dispatcher-rate-limit-backoff · PR: https://github.com/jakildev/IrredenEngine/pull/526
- [x] **T-120** — fleet-claim worktree reservation primitives · Owner: claude/T-120-fleet-claim-reservations · PR: https://github.com/jakildev/IrredenEngine/pull/529
- [x] **T-110** — fleet-claim: stackable-on claim mode + helpers · Owner: claude/T-110-stackable-on-claim · PR: https://github.com/jakildev/IrredenEngine/pull/525
- [x] **T-114** — Reviewer: cross-author stacked-PR awareness · Owner: claude/T-114-reviewer-stacked-pr-awareness · PR: https://github.com/jakildev/IrredenEngine/pull/518
- [x] **T-101** — Lua-driven ECS: Lua-defined systems with archetype-batched dispatch · Owner: claude/T-101-lua-systems · PR: https://github.com/jakildev/IrredenEngine/pull/517
- [x] **T-100** — Lua-driven ECS: Lua-defined components with type inference · Owner: claude/T-100-lua-components · PR: https://github.com/jakildev/IrredenEngine/pull/508
- [x] **T-108** — Docs: replace stale fleet-babysit references in transient role docs · Owner: claude/T-108-docs-fleet-dispatcher-refs · PR: https://github.com/jakildev/IrredenEngine/pull/511
- [x] **T-107** — Fleet: fix pane_is_running_claude for macOS version-string process names · Owner: claude/T-107-pane-is-running-claude-fix · PR: https://github.com/jakildev/IrredenEngine/pull/510
- [x] **T-105** — Fleet: project_queue_manager trigger on PR-merge events · Owner: claude/T-105-qm-pr-merge-trigger · PR: https://github.com/jakildev/IrredenEngine/pull/509
- [x] **T-096** — Sprite: SPRITES_TO_SCREEN instanced draw + iso z-sort · Owner: claude/T-096-sprites-to-screen · PR: https://github.com/jakildev/IrredenEngine/pull/507
- [x] **T-106** — Fleet: timeout-wrap tmux send-keys in fleet-dispatcher · Owner: claude/T-106-timeout-tmux-send-keys · PR: https://github.com/jakildev/IrredenEngine/pull/506
- [x] **T-099** — Lua-driven ECS: design doc · Owner: claude/T-099-lua-ecs-design-doc · PR: https://github.com/jakildev/IrredenEngine/pull/496
