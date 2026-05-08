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
  - **Blocked by:** (none)
  - **Acceptance:** (1) `grep -rn 'C_OccupancyGrid\|OccupancyGrid\|kBufferIndex_OccupancyGrid\|BUILD_OCCUPANCY_GRID\|occupancyGetBit'` returns zero hits across `engine/`, `creations/`, `test/`; (2) all lighting demos (`IRLightingCombined`, `IRLightingSunShadow`, `IRLightingEmissive`, `IRLightingPoint`, `IRLightingSpot`, `IRShapeDebug`) render identically to pre-deletion reference via `render-debug-loop`; (3) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`; (4) CLAUDE.md phased-out sections from T-071 removed; one-line note added pointing to the PR that retired the grid
  - **Issue:** #429
  - **Notes:** Zero-design task — delete only, no new behavior. Trivial PR once T-072 consumers are gone. If a hidden consumer is found, bounce it upstream to T-072 rather than partially deleting. Also: promote `kBufferIndex_SunShadowDepthMap = 28` as canonical slot-28 name (retiring the alias); delete CPU↔GPU `roundHalfUp` parity contract docs if no other consumer depends on it (verify light-volume GPU port first). CAUTION: T-091 (issue #428) was manually closed without a merged PR — the AO migration via trixelDistances was NOT completed; verify that `c_compute_voxel_ao` still does not read `OccupancyGridBuffer` before starting this deletion, or re-file the AO migration work first.
  - **Links:**


- [~] **Sprite: Lua bindings + sprite_demo creation** — expose sprite/animation API as ir.sprite.* Lua surface; scaffold sprite_demo demo creation exercising all loop modes and depth sort
  - **ID:** T-098
  - **Area:** engine/script, creations/demos/sprite_demo
  - **Model:** sonnet
  - **Owner:** claude/T-098-sprite-lua-demo
  - **Blocked by:** (none)
  - **Acceptance:** (1) fleet-run IRSpriteDemo launches and shows multiple animated sprites without crashing; (2) fleet-run IRSpriteDemo --auto-screenshot 10 produces committed shot list; (3) visual confirmation all three loop modes work and back-to-front sort is correct; (4) fleet-build clean on linux-debug AND macos-debug
  - **Issue:** #286
  - **Notes:** Part of #14 (sprite-rendering epic). Use create-creation skill for scaffold. Bindings on ir.sprite.* not ir.render.*. Generated art asset acceptable. Depends on T-095, T-096, T-097.
  - **Links:**


- [ ] **Lua-driven ECS: pipeline composition + enum bindings + modifier-framework bindings** — bind IRSystem::registerPipeline, SystemName/GameSystemName enums, and modifier framework (C_Modifiers, transforms, C_ResolvedFields, field-binding registry) to Lua; demo creation with pure-Lua initSystems
  - **ID:** T-102
  - **Area:** engine/script, engine/system
  - **Model:** opus
  - **Owner:** free
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

- [~] **Scout: pre-compute stackable_blocker_pr field** — for each single-T-NNN-blocked task with an open blocker PR, attach stackable_blocker_pr {number, headRefName, author} to the task entry in state.json
  - **ID:** T-111
  - **Area:** tooling
  - **Model:** opus
  - **Owner:** claude/T-111-scout-stackable-blocker-pr
  - **Blocked by:** (none)
  - **Acceptance:** (1) state.json tasks for single-T-NNN-blocked tasks with an open claude/T-NNN-* PR carry stackable_blocker_pr field; (2) tasks with multiple blockers have no stackable_blocker_pr (v1 guard); (3) tasks with no open blocker PR have no stackable_blocker_pr; (4) scout runs without error after change; (5) projections/queue-manager.json and other projections unaffected if they don't consume the new field
  - **Issue:** (none)
  - **Notes:** PR 2 of 6 for #501. Full architect plan in .fleet/plans/T-110.md. Key files: scripts/fleet/fleet-state-scout. Engine + game populated but only engine consumed in v1 (game guard in T-112).
  - **Links:**

- [ ] **Worker role docs: stackable-blocked fallback pickup tier** — update sonnet-author and opus-worker step 3 with two-tier task pickup: unblocked first, stackable-blocked only if no unblocked tasks exist
  - **ID:** T-112
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-111
  - **Acceptance:** (1) role-sonnet-author.md and role-opus-worker.md step 3 describe two-tier pickup with only-if-no-unblocked ordering; (2) stacked PR opens with --base $base --label fleet:stacked via commit-and-push cursor-stack mode; (3) multi-blocker tasks (Blocked by: T-A, T-B) explicitly excluded from fallback tier; (4) engine-only guard: game-side stackable tasks not picked up in v1; (5) fleet-claim claim --stackable-on invocation described in worker steps
  - **Issue:** (none)
  - **Notes:** PR 3 of 6 for #501. Full architect plan in .fleet/plans/T-110.md. Must land after T-110 and T-111. Key files: .claude/commands/role-sonnet-author.md, .claude/commands/role-opus-worker.md.
  - **Links:**

- [ ] **Merger: cascade rebase on upstream force-push for stacked PRs** — new merger-loop step: detect stacked child PRs whose upstream tip moved, rebase clean children, label conflicted children fleet:needs-base-update
  - **ID:** T-113
  - **Area:** tooling
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) merger loop detects fleet:stacked PRs with open base PR whose tip SHA moved; (2) clean rebase -> git push --force-with-lease, prior approval labels preserved; (3) conflict -> git rebase --abort, fleet:needs-base-update label added, conflict-files comment posted; (4) fleet-labels updated with fleet:needs-base-update registration; (5) blocker's author never touches child branch; (6) sidecar-based SHA tracking avoids re-processing already-rebased children
  - **Issue:** (none)
  - **Notes:** PR 4 of 6 for #501. Full architect plan in .fleet/plans/T-110.md. Key files: .claude/commands/role-merger.md, scripts/fleet/fleet-labels. Runs independently from T-112.
  - **Links:**

- [ ] **Docs: cross-author stacking lifecycle in FLEET.md** — new "Cross-author stacking (scheduler)" subsection covering full lifecycle (claim → PR open → reviewer gate → upstream feedback rebase → upstream merge re-target), Q1/Q2/Q3 decisions, and v1 limitations
  - **ID:** T-115
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-112, T-113
  - **Acceptance:** (1) docs/agents/FLEET.md has "Cross-author stacking (scheduler)" section with full lifecycle walkthrough; (2) Q1 (only-if-no-unblocked), Q2 (merger-driven hybrid rebase), Q3 (single-blocker-only v1) decisions documented; (3) v1 limitations listed (engine-only, single-blocker, multi-blocker not eligible); (4) pointer to fleet-claim and fleet-state-scout for implementation detail; (5) no other docs changed
  - **Issue:** #501
  - **Notes:** PR 6 of 6 for #501 — lands last, closes the tracking issue. Full architect plan in .fleet/plans/T-110.md.
  - **Links:**

- [ ] **Render: per-canvas light scope via CHILD_OF relation** — scope C_LightSource to a specific canvas; lights become children of their target canvas via CHILD_OF; lighting systems iterate per-canvas through existing RelationParams machinery
  - **ID:** T-116
  - **Area:** engine/render, engine/prefabs/irreden/render
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) two-canvas demo where a C_LightSource parented via CHILD_OF to canvas A produces no lighting contribution in canvas B; (2) existing single-canvas lighting demos (IRLightingCombined, IRLightingPoint, IRLightingSpot) unaffected; (3) fleet-build clean on linux-debug AND macos-debug
  - **Issue:** #363
  - **Notes:** Follow-up from lighting-fidelity-polish PR (audit finding #11). Preferred implementation: Option B (CHILD_OF relation) over Option A (explicit scope tag) — composes with existing RelationParams machinery and falls out of existing system patterns. Every C_LightSource currently contributes to every canvas with a C_CanvasLightVolume; scoping is useful for UI/inset canvases that should not receive world-space lights.
  - **Links:**

- [~] **Render: SDF occlusion in point/spot light line-of-sight** — add SDF-shape pass to detail::hasLineOfSight so C_ShapeDescriptor entities tagged C_LightBlocker block point/spot propagation
  - **ID:** T-117
  - **Area:** engine/render, engine/prefabs/irreden/render
  - **Model:** opus
  - **Owner:** claude/T-117-sdf-occlusion-light-los
  - **Blocked by:** (none)
  - **Acceptance:** (1) demo with a C_ShapeDescriptor (box) placed between a point light and a voxel surface produces a visible shadow on the surface; (2) per-shape cost bounded by sun-cone-style culling (only shapes tagged C_LightBlocker within the light radius are evaluated); (3) fleet-build clean on linux-debug AND macos-debug
  - **Issue:** #364
  - **Notes:** Follow-up from lighting-fidelity-polish PR. The SDF-from-occupancy-grid removal left C_ShapeDescriptor entities invisible to point/spot LOS in system_compute_light_volume.hpp::detail::hasLineOfSight. Fix: for each step along the light ray, evaluate IRMath::SDF::evaluate for each C_ShapeDescriptor entity tagged C_LightBlocker; if any shape returns <= kSurfaceThreshold the ray is blocked. If GPU-side propagation rewrite (#359/#360) lands first, this may fold into the compute pass instead.
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

- [~] **Fleet: usage-limit back-off for fleet-dispatcher transient workers** — detect rate-limit exit in transient worker panes and apply per-pane cooldown before re-dispatch, mirroring fleet-babysit's LIMIT_DELAY pattern
  - **ID:** T-119
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** claude/T-119-dispatcher-rate-limit-backoff
  - **Blocked by:** (none)
  - **Acceptance:** (1) when a transient worker pane exits with a rate-limit error, dispatcher marks that pane with a cooldown file and skips re-dispatch for FLEET_DISPATCHER_LIMIT_DELAY seconds (default 900); (2) other panes not in cooldown dispatch normally (per-pane isolation — parallelism preserved); (3) dispatcher log records cooldown start once per pane, not every tick; (4) FLEET_DISPATCHER_LIMIT_DELAY is env-overridable; (5) fleet-babysit behavior unchanged; (6) existing non-rate-limited panes unaffected
  - **Issue:** #520
  - **Notes:** Mirror fleet-babysit LIMIT_DELAY=900 pattern (scripts/fleet/fleet-babysit lines 96 and 563-565). Two detection approaches in issue: Option A (parse pane scrollback for rate-limit pattern), Option B (wrap claude invocation with exit-code reporter). Per-pane keying (not per-role) matches PR #498 per-pane dispatch tracking infrastructure. Key file: scripts/fleet/fleet-dispatcher.
  - **Links:**

- [~] **Fleet: fleet-claim worktree reservation primitives** — new reserve, release-worktree, worktree-for-task, reservation-of, list-reservations subcommands; ~/.fleet/reservations/<worktree>.json storage; atomic create-or-fail; claim/release integration
  - **ID:** T-120
  - **Area:** tooling
  - **Model:** opus
  - **Owner:** claude/T-120-fleet-claim-reservations
  - **Blocked by:** (none)
  - **Stack:** T-120..T-125 worktree-reservations
  - **Acceptance:** (1) fleet-claim reserve <worktree> <task-id> writes reservation JSON atomically, fails if already reserved; (2) release-worktree clears reservation idempotently; (3) worktree-for-task prints worktree name or empty; (4) reservation-of prints task_id or empty; (5) list-reservations shows all current entries; (6) existing claim/release flow unaffected; (7) minimal bash test harness covers all new paths
  - **Issue:** (none)
  - **Notes:** PR 1 of 6 for #521 (worktree-as-reservation epic). Full plan in .fleet/plans/T-120.md. Atomicity via mkdir-lock pattern at ~/.fleet/reservations/<name>.lock/. When fleet-claim claim runs, also writes reservation if agent matches a worktree basename. fleet-up clear-all should clear ~/.fleet/reservations/ but leave dirty worktree state. Confirm ~/bin/ install mechanism with human at PR time — fleet-claim is not in the engine repo.
  - **Links:**

- [ ] **Fleet: dispatcher reservation-aware pane selection** — when firing a role-X iteration, route to reserved pane for role first; free-pane fallback second; defer if in-flight+reserved >= cap
  - **ID:** T-121
  - **Area:** tooling
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-120
  - **Stack:** T-120..T-125 worktree-reservations
  - **Acceptance:** (1) dispatcher routes to reserved pane when a reservation exists for matching role; (2) free-pane fallback works when no reservation matches; (3) cap-check defers dispatch if in-flight+reserved >= per-role cap; (4) fleet-claim reserve in test → dispatcher routes to reserved pane; remove → routes to free pane
  - **Issue:** (none)
  - **Notes:** PR 2 of 6 for #521. Full plan in .fleet/plans/T-120.md. New helper: fleet-claim reservation-role <worktree> → prints role tag from TASKS.md entry. Key file: ~/bin/fleet-dispatcher. @fleet-role kept as fallback signal; reservations take priority.
  - **Links:**

- [~] **Fleet: role docs startup reservation check** — new step 0.5 in opus-worker and sonnet-author: check reservation-of on startup; if found, checkout reserved branch and skip pickup; release-worktree before start-next-task
  - **ID:** T-122
  - **Area:** docs, tooling
  - **Model:** sonnet
  - **Owner:** claude/T-122-role-startup-reservation
  - **Blocked by:** T-121
  - **Stack:** T-120..T-125 worktree-reservations
  - **Acceptance:** (1) role-opus-worker.md and role-sonnet-author.md include step 0.5 checking fleet-claim reservation-of <worktree>; (2) if reservation found, checkout reserved branch and skip planning/pickup steps; (3) step 12 calls fleet-claim release-worktree before start-next-task; (4) dry-run test: stale reservation in place → role exits without double-claim; (5) sonnet-reviewer, queue-manager, merger unaffected
  - **Issue:** (none)
  - **Notes:** PR 3 of 6 for #521. Full plan in .fleet/plans/T-120.md. Files: .claude/commands/role-opus-worker.md, .claude/commands/role-sonnet-author.md. Architects exempt (dedicated panes, babysit lifecycle). Step 8 (design-blocked escalation) keeps reservation — next iteration resumes same worktree.
  - **Links:**

- [ ] **Fleet: worktree naming migration (opus-worker-N → worktree-N)** — fleet-up provisions generic worktree-N names; one-shot migration script renames clean worktrees via git worktree move; dirty worktrees → escalation, not auto-wipe; MIGRATION-WORKTREES.md walkthrough
  - **ID:** T-123
  - **Area:** tooling, docs
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-122
  - **Stack:** T-120..T-125 worktree-reservations
  - **Acceptance:** (1) fleet-up provisions worktree-N names (opus-worker-1 → worktree-1, sonnet-fleet-1 → worktree-3, etc.); (2) migration script renames clean worktrees via git worktree move; (3) dirty worktrees file escalation issue instead of auto-wiping; (4) docs/agents/MIGRATION-WORKTREES.md included; (5) opus-architect-1 / game-architect-1 untouched; (6) FLEET.md + FLEET-CACHE.md updated
  - **Issue:** (none)
  - **Notes:** PR 4 of 6 for #521, parallelizable with T-125 after T-122 lands. Full plan in .fleet/plans/T-120.md. Human must run migration script locally (touches machine state). All role docs that hardcode worktree names updated in same PR.
  - **Links:**

- [ ] **Fleet: stuck-worktree staleness escalation** — extend fleet-claim check-stale: reservations older than 24h file a fleet:stuck-worktree issue with dirty-file capture; one-shot per reservation via .escalated flag
  - **ID:** T-124
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-120
  - **Stack:** T-120..T-125 worktree-reservations
  - **Acceptance:** (1) fleet-claim check-stale detects reservations older than 24h; (2) files engine-repo issue with fleet:stuck-worktree label per stale reservation (title: "fleet: stuck worktree <name> reserved for T-NNN"); (3) issue body includes reservation JSON, dirty file list, last commit; (4) one-shot via ~/.fleet/reservations/<name>.escalated flag; (5) existing check-stale task-claim behavior unaffected
  - **Issue:** (none)
  - **Notes:** PR 5 of 6 for #521, can run parallel to T-122+ after T-120. Full plan in .fleet/plans/T-120.md. New GitHub label fleet:stuck-worktree needed on engine repo — create it or have human create before PR merges. Files: ~/bin/fleet-claim, ~/bin/fleet-up.
  - **Links:**

- [ ] **Fleet: per-role concurrency cap config + dispatcher enforcement** — fleet-up.conf per-role concurrency field; dispatcher defers dispatch when in-flight+reserved >= cap[role]; default opus-worker=2, sonnet-fleet=4
  - **ID:** T-125
  - **Area:** tooling
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-122
  - **Stack:** T-120..T-125 worktree-reservations
  - **Acceptance:** (1) fleet-up.conf gains per-role concurrency config (opus-worker: concurrency=2, sonnet-fleet: concurrency=4); (2) dispatcher defers when in-flight+reserved for role >= cap; (3) with concurrency=2, third concurrent opus-worker iteration deferred even with free worktrees; (4) env-overridable; (5) per-pane parallelism preserved for roles under cap; (6) default values preserve current behavior
  - **Issue:** (none)
  - **Notes:** PR 6 of 6 for #521, parallelizable with T-123 after T-122. Full plan in .fleet/plans/T-120.md. Files: ~/bin/fleet-up (config schema), ~/bin/fleet-dispatcher (cap-check at dispatch time). Soft cap — orthogonal to reservations; reservations protect dirty-state continuity, cap protects credit budget.
  - **Links:**

---

## In progress

<!-- Tasks currently being worked on. Mirror of [~] items above. -->


---

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

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
- [x] **T-097** — Sprite: C_SpriteAnimation + animation-advance system · Owner: claude/T-097-sprite-animation · PR: https://github.com/jakildev/IrredenEngine/pull/495
- [x] **T-095** — Sprite: sprite-sheet asset format + loader · Owner: claude/T-095-sprite-sheet-loader · PR: https://github.com/jakildev/IrredenEngine/pull/494
- [x] **T-094** — Render: camera-anchor GPU light volume for fidelity past static window · Owner: claude/render-camera-anchored-grids · PR: https://github.com/jakildev/IrredenEngine/pull/450
- [x] **T-072** — Render: GPU-side light-volume propagation (jump flooding / iterative dilation) · Owner: claude/render-light-volume-gpu · PR: https://github.com/jakildev/IrredenEngine/pull/448
- [x] **T-093** — Input: fix system_hitbox_mouse_test projection under non-zero camera yaw · Owner: claude/T-093-hitbox-yaw · PR: https://github.com/jakildev/IrredenEngine/pull/436
- [x] **T-089** — Modifier framework: LAMBDA_MODIFIER_DECAY system + stateful-lambda design · Owner: opus-worker-2 · PR: https://github.com/jakildev/IrredenEngine/pull/351
- [x] **T-071** — Render: delete legacy sun-shadow paths (analytic caster + occupancy DDA) · Owner: claude/T-071-delete-occupancy-grid · PR: https://github.com/jakildev/IrredenEngine/pull/423
- [x] **T-057** — Render/input: screen-to-world picking under Z-yaw · Owner: claude/T-057-picking-yaw-inverse · PR: https://github.com/jakildev/IrredenEngine/pull/424
- [x] **T-088** — Modifier demo creation: modifier_demo visual showcase · Owner: claude/T-088-modifier-demo · PR: https://github.com/jakildev/IrredenEngine/pull/427
- [x] **T-090** — Fleet: queue-manager bidirectional consistency pass · Owner: claude/T-090-queue-bidirectional-consistency · PR: https://github.com/jakildev/IrredenEngine/pull/425


