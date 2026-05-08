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









- [ ] **Render: final occupancy-grid teardown (drop BUILD_OCCUPANCY_GRID + C_OccupancyGrid)** — pure deletion after T-091 (AO) and T-072 (light-volume) land; remove grid system, component, SSBO, constants, and CLAUDE.md phased-out sections
  - **ID:** T-092
  - **Area:** engine/render, engine/prefabs/irreden/render, shaders/glsl, shaders/metal
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `grep -rn 'C_OccupancyGrid\|OccupancyGrid\|kBufferIndex_OccupancyGrid\|BUILD_OCCUPANCY_GRID\|occupancyGetBit'` returns zero hits across `engine/`, `creations/`, `test/`; (2) all lighting demos (`IRLightingCombined`, `IRLightingSunShadow`, `IRLightingEmissive`, `IRLightingPoint`, `IRLightingSpot`, `IRShapeDebug`) render identically to pre-deletion reference via `render-debug-loop`; (3) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`; (4) CLAUDE.md phased-out sections from T-071 removed; one-line note added pointing to the PR that retired the grid
  - **Issue:** #429
  - **Notes:** Zero-design task — delete only, no new behavior. Trivial PR once T-072 consumers are gone. If a hidden consumer is found, bounce it upstream to T-072 rather than partially deleting. Also: promote `kBufferIndex_SunShadowDepthMap = 28` as canonical slot-28 name (retiring the alias); delete CPU↔GPU `roundHalfUp` parity contract docs if no other consumer depends on it (verify light-volume GPU port first). CAUTION: T-091 (issue #428) was manually closed without a merged PR — the AO migration via trixelDistances was NOT completed; verify that `c_compute_voxel_ao` still does not read `OccupancyGridBuffer` before starting this deletion, or re-file the AO migration work first.
  - **Links:**

- [~] **Sprite: SPRITES_TO_SCREEN instanced draw + iso z-sort** — new pipeline stage renders sprite entities with one instanced quad draw, CPU-side iso depth sort, GLSL+MSL backends
  - **ID:** T-096
  - **Area:** engine/prefabs/irreden/render/systems, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** claude/T-096-sprites-to-screen
  - **Blocked by:** (none)
  - **Acceptance:** (1) single sprite at origin renders at screen center at zoom 1 with pixel-perfect scaling; (2) multiple sprites composite back-to-front by iso depth; (3) 50+ sprites cost exactly one drawArraysInstanced call; (4) fleet-build clean on linux-debug AND macos-debug; (5) render-debug-loop screenshots committed to docs/pr-screenshots/
  - **Issue:** #284
  - **Notes:** Part of #14 (sprite-rendering epic). Replaces system_sprites_to_screen.hpp stub. Screen-composite layer at FRAMEBUFFER_TO_SCREEN stage (not trixel content). Follow backend-parity skill for GLSL/MSL port. Depends on T-095.
  - **Links:**

- [ ] **Sprite: Lua bindings + sprite_demo creation** — expose sprite/animation API as ir.sprite.* Lua surface; scaffold sprite_demo demo creation exercising all loop modes and depth sort
  - **ID:** T-098
  - **Area:** engine/script, creations/demos/sprite_demo
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-096
  - **Acceptance:** (1) fleet-run IRSpriteDemo launches and shows multiple animated sprites without crashing; (2) fleet-run IRSpriteDemo --auto-screenshot 10 produces committed shot list; (3) visual confirmation all three loop modes work and back-to-front sort is correct; (4) fleet-build clean on linux-debug AND macos-debug
  - **Issue:** #286
  - **Notes:** Part of #14 (sprite-rendering epic). Use create-creation skill for scaffold. Bindings on ir.sprite.* not ir.render.*. Generated art asset acceptable. Depends on T-095, T-096, T-097.
  - **Links:**

- [~] **Lua-driven ECS: Lua-defined components with type inference** — add IComponentDataLuaTyped + EntityManager::registerComponentDynamic; Lua surface: IRComponent.register with single native-SoA storage tier and type inference; modifier field bindings auto-registered
  - **ID:** T-100
  - **Area:** engine/entity, engine/script
  - **Model:** opus
  - **Owner:** claude/T-100-lua-components
  - **Blocked by:** (none)
  - **Acceptance:** (1) register C_Hp { current = 100, max = 100 } from Lua; attach to 100 entities; C++ test reads back Hp.current as a real int32 column, not sol::object; (2) explicit typed form { field = {type="float", default=100} } accepted; (3) unresolvable field type raises Lua error at registration naming the offending field; (4) auto-registered field binding "Hp.current" visible to modifier resolver; (5) identity rule: registering a duplicate name fails fast; (6) IREntity.addLuaComponent, getLuaComponent, removeLuaComponent bound; (7) fleet-build clean on linux-debug
  - **Issue:** #488
  - **Notes:** PR 2 of 6 for parent epic #293. Full architect plan in .fleet/plans/T-100.md. Blocked by T-099 (design doc). Key files: engine/entity/include/irreden/entity/i_component_data.hpp (add IComponentDataLuaTyped impl), engine/entity/include/irreden/entity/entity_manager.hpp (add registerComponentDynamic), engine/script/include/irreden/script/lua_script.hpp (add registerComponent).
  - **Links:**

- [ ] **Lua-driven ECS: Lua-defined systems with archetype-batched dispatch** — add IRSystem::createSystemDynamic + LuaScript::registerSystem; archetype-batched dispatch (one sol::function call per archetype per tick); C++ and Lua share same ComponentId space
  - **ID:** T-101
  - **Area:** engine/system, engine/script
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-100
  - **Acceptance:** (1) Lua system iterates (C_Position3D, C_Velocity3D) both C++-defined plus Lua-defined C_Marker tag; C++ render system sees Lua-written C_Position3D changes next frame; (2) separate test shows one sol::function invocation per archetype per tick, not per-entity; (3) unbound C++ type requested in system fails fast with error pointing at lua_component_pack; (4) registerSystem returns a SystemId Lua can pass to registerPipeline; (5) fleet-build clean on linux-debug
  - **Issue:** #489
  - **Notes:** PR 3 of 6 for parent epic #293. Full architect plan in .fleet/plans/T-101.md. Blocked by T-100 (Lua components). Key files: engine/system/include/irreden/system/ir_system.hpp (add createSystemDynamic), engine/system/include/irreden/system/system_manager.hpp (DynamicSystem dispatch path), engine/script/include/irreden/script/lua_script.hpp (add registerSystem).
  - **Links:**

- [ ] **Lua-driven ECS: pipeline composition + enum bindings + modifier-framework bindings** — bind IRSystem::registerPipeline, SystemName/GameSystemName enums, and modifier framework (C_Modifiers, transforms, C_ResolvedFields, field-binding registry) to Lua; demo creation with pure-Lua initSystems
  - **ID:** T-102
  - **Area:** engine/script, engine/system
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-101
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

- [~] **Fleet: project_queue_manager trigger on PR-merge events** — fix scout's project_queue_manager projection to include closed fleet:queued issues matched against TASKS.md in-progress entries, making the trigger chain self-sustaining on PR merges
  - **ID:** T-105
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** claude/T-105-qm-pr-merge-trigger
  - **Blocked by:** (none)
  - **Acceptance:** (1) merging a PR that closes a fleet:queued issue fires a queue-manager trigger within the next scout cycle without relying on periodic re-arm; (2) project_queue_manager returns a needs_flip entry for any in-progress TASKS.md task whose linked issue is closed; (3) existing periodic re-arm from PR #499 can be reverted or lengthened once the trigger chain is self-sustaining
  - **Issue:** #500
  - **Notes:** Root cause: project_queue_manager projection excludes fleet:queued issues (filtered from human_approved) so closed issues are invisible. Fix: add fetch of closed fleet:queued issues, cross-reference with TASKS.md in_progress entries by Issue: #N. Scout already parses in_progress from TASKS.md. Observed: T-096 and T-100 were un-claimable 4.5h after T-095/T-097/T-099 merged.
  - **Links:**

- [~] **Fleet: fix pane_is_running_claude for macOS version-string process names** — invert cleanup logic to treat any non-shell pane_current_command as running claude instead of matching hard-coded names
  - **ID:** T-107
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** claude/T-107-pane-is-running-claude-fix
  - **Blocked by:** (none)
  - **Acceptance:** (1) pane_is_running_claude returns yes when tmux reports pane_current_command as a version-string like 2.1.132; (2) returns no for bash/zsh/sh/fish; (3) cleanup_stale_dispatches no longer prematurely deletes dispatch records for active claude panes on macOS
  - **Issue:** #503
  - **Notes:** Option B from issue preferred (inverted logic: not-shell = running something) over Option A (walk pgrep process tree) for simplicity. Observed: weird macOS process name caused cleanup to treat an active claude pane as returned-to-shell, triggering premature dispatch record deletion.
  - **Links:**

- [~] **Docs: replace stale fleet-babysit references in transient role docs** — sweep 6 role docs replacing fleet-babysit with fleet-dispatcher for transient-role relaunch descriptions
  - **ID:** T-108
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** claude/T-108-docs-fleet-dispatcher-refs
  - **Blocked by:** (none)
  - **Acceptance:** (1) grep for fleet-babysit in role-opus-reviewer.md, role-opus-worker.md, role-sonnet-author.md, role-sonnet-reviewer.md, role-queue-manager.md, role-merger.md returns zero hits in relaunch/scheduling/backoff contexts; (2) fleet-babysit references in role-opus-architect.md and role-game-architect.md are preserved; (3) usage-limit backoff description matches PR #497 pattern: flag in iteration summary, human intervenes
  - **Issue:** #504
  - **Notes:** PR #497 fixed human-visible banner/exit lines. This covers deeper operational sections (~20-30 line touches across 6 files). Strictly doc edits, no code changes. Surviving stale references listed in issue body.
  - **Links:**

- [ ] **Lua-driven ECS: field index + index-style accessors for zero-string hot path** — expose field column index in registration handle; add `IREntity.getLuaField`/`setLuaField` for zero-string per-tick access; document two-tier accessor contract in engine/script/CLAUDE.md
  - **ID:** T-109
  - **Area:** engine/script, engine/entity
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-100
  - **Acceptance:** (1) `IRComponent.register` per-field handle carries `index`; (2) `IREntity.getLuaField`/`setLuaField` work by field index with no string lookup or table allocation; (3) out-of-range `fieldIndex` raises Lua error naming the offending index; (4) table-style `addLuaComponent`/`getLuaComponent` unchanged; (5) tests cover index round-trip, out-of-range error, and table-style regression; (6) `fleet-build --target IrredenEngineTest` clean on `linux-debug` and `macos-debug`; (7) `engine/script/CLAUDE.md` two-tier accessor section added
  - **Issue:** #514
  - **Notes:** Follow-up to T-100 (PR #508). Additive — no changes to existing table-style API. Enables Lua systems to cache `field.index` once at script load and call `getLuaField`/`setLuaField` per tick with zero string work. Unblocks T-101 to commit to a zero-string per-tick contract. Key files: `engine/script/src/lua_script.cpp` (add field.index + getLuaField/setLuaField bindings), `engine/script/include/irreden/script/i_component_data_lua_typed.hpp` (add readFieldAt/writeFieldAt), `test/script/lua_component_register_test.cpp` (index accessor tests).
  - **Links:**

---

## In progress

<!-- Tasks currently being worked on. Mirror of [~] items above. -->


---

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

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
- [x] **T-087** — Sprite rendering: C_Sprite / C_SpriteSheet components + design note · Owner: claude/T-087-sprite-components · PR: https://github.com/jakildev/IrredenEngine/pull/417
- [x] **T-086** — Input: audit and document gamepad support · Owner: claude/T-086-gamepad-audit · PR: https://github.com/jakildev/IrredenEngine/pull/415
- [x] **T-070** — Render: screen-space sun shadow map — add bake pass (flag-guarded) · Owner: claude/T-070-screen-space-sun-shadow-bake · PR: https://github.com/jakildev/IrredenEngine/pull/406
- [x] **T-083** — render/metal: fix getEntityIdAtMouseTrixel stale-pointer UAF after subData orphan-on-write · Owner: claude/T-083-metal-uaf-stale-pointer · PR: https://github.com/jakildev/IrredenEngine/pull/416
- [x] **T-085** — fleet: option-B handoff should set fleet:changes-made before removing fleet:needs-fix · Owner: claude/T-085-changes-made-sequencing · PR: https://github.com/jakildev/IrredenEngine/pull/414
- [x] **T-084** — merger: dedupe semantic-conflict re-comments when sha pair unchanged · Owner: claude/T-084-merger-dedupe-conflict-comments · PR: https://github.com/jakildev/IrredenEngine/pull/413
- [x] **T-068** — Render/shader: SDF fast-path redesign under non-zero Z-yaw + snap-mode/voxel-pool alignment · Owner: claude/T-068-sdf-cardinal-snap-align · PR: https://github.com/jakildev/IrredenEngine/pull/412
- [x] **T-058** — Render: screen-space 2D residual yaw composite pass (GLSL+MSL) · Owner: claude/T-058-screen-residual-rotate · PR: https://github.com/jakildev/IrredenEngine/pull/405

