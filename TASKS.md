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


- [~] **Render/input: screen-to-world picking under Z-yaw** — update picking inverse to compose `R2D(-residualYaw)` then `R(-rasterYaw)·M⁻¹`; audit duplicate transform copies
  - **ID:** T-057
  - **Area:** engine/render, engine/input
  - **Model:** opus
  - **Owner:** claude/T-057-picking-yaw-inverse
  - **Blocked by:** (none)
  - **Stack:** T-054..T-058 z-yaw-pipeline
  - **Acceptance:** (1) picking inverse composes `R2D(-residualYaw)` then `R(-rasterYaw)·M⁻¹` per plan; (2) correct world coords at yaw=0 (no regression for any existing consumer); (3) correct world coords at ≥4 non-cardinal yaw values; (4) audit of duplicate screen↔world transform copies in `engine/render/` and input-side consumers complete; (5) `fleet-build --target IRShapeDebug` clean
  - **Issue:** #313
  - **Notes:** Child 5 of 5 of epic #310. Sequenced last — inverts the full composition once both T-055 (cardinal raster) and the residual composite pass (T-058) land. Full plan: `.fleet/plans/T-054.md`. Prior PR #418 was closed without merging.
  - **Links:**


- [~] **Render: screen-space sun shadow map — delete occupancy grid + analytic caster paths** — remove `BUILD_OCCUPANCY_GRID`, `C_OccupancyGrid`, `SunShadowShapeCasterBuffer`, `analyticShapeShadowHit`, and in-shader SDF helpers after T-070 establishes the screen-space path
  - **ID:** T-071
  - **Area:** engine/render, engine/prefabs/irreden/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** claude/T-071-delete-occupancy-grid
  - **Blocked by:** (none)
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


- [~] **Modifier demo creation: modifier_demo visual showcase** — scaffold `creations/demos/modifier_demo/` and implement an 8-cube interactive demo exercising every modifier framework capability via keyboard + HUD
  - **ID:** T-088
  - **Area:** creations/demos/modifier_demo
  - **Model:** sonnet
  - **Owner:** claude/T-088-modifier-demo
  - **Blocked by:** (none)
  - **Acceptance:** (1) `fleet-run IRModifierDemo` launches and shows a row of moving cubes; (2) each numbered key (1–8) triggers the corresponding modifier capability with an obvious visual change; (3) on-screen HUD shows resolved values matching active modifiers (manual verify on ≥3 capabilities); (4) `fleet-run IRModifierDemo --auto-screenshot 12` produces the committed shot list; (5) builds clean on `linux-debug` AND `macos-debug`
  - **Issue:** #307
  - **Notes:** Child 5 of 5 of modifier-framework epic #302. Children 1–4 (design doc #303, core runtime #304, pattern migration #305, Lua bindings #306) are all shipped. Full plan: `.fleet/plans/T-088.md`. Use `create-creation` skill for scaffolding. Lua wiring: register fields, spawn cubes, key handlers, HUD via trixel-text rendering. Key 5 global-slow needs `C_NoGlobalModifiers` tag binding (add in Lua binding layer if missing). Key 6 sinusoidal lambda needs `ir.time()` (plumb if absent). Key 7 source-kill verifies framework's entity-destruction sweep removes only the right source's modifiers. Keep cube count and Lua complexity low — ~200 lines of Lua is enough.
  - **Links:**



- [ ] **Modifier framework: LAMBDA_MODIFIER_DECAY system + stateful-lambda design** — add `system_modifier_lambda_decay.hpp` to auto-expire lambda modifiers by `ticksRemaining_`, and design (but defer implementation of) stateful-lambda support
  - **ID:** T-089
  - **Area:** engine/prefabs/irreden/common
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** Part 1 — (1) `LAMBDA_MODIFIER_DECAY` system exists, decrements `ticksRemaining_` per tick, removes expired entries from `C_LambdaModifiers`; (2) test covers basic decay case (push lambda modifier with ticksRemaining=60, advance 60 ticks, assert removed); (3) builds clean on `linux-debug` and `macos-debug`; (4) `engine/prefabs/irreden/common/CLAUDE.md` "Open follow-ups" updated. Part 2 acceptance to be defined by architect after design is locked.
  - **Issue:** #341
  - **Notes:** Part 1 is mechanical — mirrors `system_modifier_decay.hpp` but targets `C_LambdaModifiers`. Part 2 (stateful lambdas) is architectural: architect prefers option (c) — lambda + companion component — where state lives on entity in its own component and the lambda receives entity ID. Part 2 should be its own dedicated PR after Part 1 lands and design is locked; worker can do Part 1 standalone. Surfaced during architect review of PR #332; documented in `engine/prefabs/irreden/common/CLAUDE.md` "Open follow-ups."
  - **Links:**

- [ ] **Render: AO via trixelDistances (drop OccupancyGridBuffer)** — migrate `c_compute_voxel_ao.{glsl,metal}` off `OccupancyGridBuffer`; sample 4 face-tangent neighbour pixels in `trixelDistances` instead
  - **ID:** T-091
  - **Area:** engine/prefabs/irreden/render/systems, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-071
  - **Acceptance:** (1) `c_compute_voxel_ao.{glsl,metal}` no longer reads `OccupancyGridBuffer` / slot 28; (2) `system_compute_voxel_ao.hpp` no longer binds `OccupancyGridBuffer`; (3) AO crease-darkening within ~1 iso pixel of edge migration vs pre-migration reference (capture before PR, compare via `render-debug-loop`); (4) SDF shapes get crease AO on adjacent surfaces (regression-fix — SDF-into-occupancy step deleted in T-071); (5) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`
  - **Issue:** #428
  - **Notes:** Architect direction in `.fleet/plans/T-091.md` (derived from `~/.fleet/plans/issue-358.md` T-09X section). Blocks final occupancy teardown T-092. Approach: project world-space face tangents through iso transform once on CPU, ship in `FrameDataVoxelToTrixel` UBO; compare receiver `pos3D'` vs face-outward plane per pixel. Cost stays O(canvasPixels) with 4 texel reads per pixel.
  - **Links:**

- [ ] **Render: final occupancy-grid teardown (drop BUILD_OCCUPANCY_GRID + C_OccupancyGrid)** — pure deletion after T-091 (AO) and T-072 (light-volume) land; remove grid system, component, SSBO, constants, and CLAUDE.md phased-out sections
  - **ID:** T-092
  - **Area:** engine/render, engine/prefabs/irreden/render, shaders/glsl, shaders/metal
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-091, T-072
  - **Acceptance:** (1) `grep -rn 'C_OccupancyGrid\|OccupancyGrid\|kBufferIndex_OccupancyGrid\|BUILD_OCCUPANCY_GRID\|occupancyGetBit'` returns zero hits across `engine/`, `creations/`, `test/`; (2) all lighting demos (`IRLightingCombined`, `IRLightingSunShadow`, `IRLightingEmissive`, `IRLightingPoint`, `IRLightingSpot`, `IRShapeDebug`) render identically to pre-deletion reference via `render-debug-loop`; (3) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`; (4) CLAUDE.md phased-out sections from T-071 removed; one-line note added pointing to the PR that retired the grid
  - **Issue:** #429
  - **Notes:** Zero-design task — delete only, no new behavior. Trivial PR once T-091 and T-072 consumers are gone. If a hidden consumer is found, bounce it upstream to T-091 or T-072 rather than partially deleting. Also: promote `kBufferIndex_SunShadowDepthMap = 28` as canonical slot-28 name (retiring the alias); delete CPU↔GPU `roundHalfUp` parity contract docs if no other consumer depends on it (verify light-volume GPU port first).
  - **Links:**

- [ ] **Input: fix system_hitbox_mouse_test projection under non-zero camera yaw** — compose `R_z(rasterYaw)` and `R2D(residualYaw)` in `HITBOX_MOUSE_TEST` forward-projection (or inverse-project mouse once in `beforeTick`) so hitbox hover tracks correctly at all visualYaw values
  - **ID:** T-093
  - **Area:** engine/prefabs/irreden/input, engine/render
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) mouse hover triggers `onHovered` for entities under cursor at `visualYaw` ∈ {0, π/8, π/4, π/2 + π/16} when entity is on-screen and within hitbox half-extents; (2) hitbox-only hover shows no regression vs master at `visualYaw=0`; (3) test synthesizes known mouse position + camera yaw and asserts `HITBOX_MOUSE_TEST` flips `hovered_` correctly under at least two non-cardinal yaws
  - **Issue:** #430
  - **Notes:** Bug surfaced during T-057 (PR #424) audit. Preferred approach (from issue): inverse-project mouse once in `beforeTick` via `inverseResidualYawOnFramebufferPixel` (from T-057) + `R_z(-rasterYaw)`, then compare in unrotated world space — O(1) per frame vs O(entities) for forward-projection. Related: epic #310.
  - **Links:**

---

## In progress

<!-- Tasks currently being worked on. Mirror of [~] items above. -->


---

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-090** — Fleet: queue-manager bidirectional consistency pass · Owner: claude/T-090-queue-bidirectional-consistency · PR: https://github.com/jakildev/IrredenEngine/pull/425
- [x] **T-087** — Sprite rendering: C_Sprite / C_SpriteSheet components + design note · Owner: claude/T-087-sprite-components · PR: https://github.com/jakildev/IrredenEngine/pull/417
- [x] **T-086** — Input: audit and document gamepad support · Owner: claude/T-086-gamepad-audit · PR: https://github.com/jakildev/IrredenEngine/pull/415
- [x] **T-070** — Render: screen-space sun shadow map — add bake pass (flag-guarded) · Owner: claude/T-070-screen-space-sun-shadow-bake · PR: https://github.com/jakildev/IrredenEngine/pull/406
- [x] **T-083** — render/metal: fix getEntityIdAtMouseTrixel stale-pointer UAF after subData orphan-on-write · Owner: claude/T-083-metal-uaf-stale-pointer · PR: https://github.com/jakildev/IrredenEngine/pull/416
- [x] **T-085** — fleet: option-B handoff should set fleet:changes-made before removing fleet:needs-fix · Owner: claude/T-085-changes-made-sequencing · PR: https://github.com/jakildev/IrredenEngine/pull/414
- [x] **T-084** — merger: dedupe semantic-conflict re-comments when sha pair unchanged · Owner: claude/T-084-merger-dedupe-conflict-comments · PR: https://github.com/jakildev/IrredenEngine/pull/413
- [x] **T-068** — Render/shader: SDF fast-path redesign under non-zero Z-yaw + snap-mode/voxel-pool alignment · Owner: claude/T-068-sdf-cardinal-snap-align · PR: https://github.com/jakildev/IrredenEngine/pull/412
- [x] **T-058** — Render: screen-space 2D residual yaw composite pass (GLSL+MSL) · Owner: claude/T-058-screen-residual-rotate · PR: https://github.com/jakildev/IrredenEngine/pull/405
- [x] **T-082** — Fleet: factor CLAUDE.md status-prose sections to prevent parallel-PR rebase conflicts · Owner: claude/T-082-status-prose-factor · PR: https://github.com/jakildev/IrredenEngine/pull/402
- [x] **T-080** — Fleet: orchestration calibration — babysit cooldown, fleet-claim git-aware, stale-status auto-flip · Owner: claude/T-080-orchestration-calibration · PR: https://github.com/jakildev/IrredenEngine/pull/396
- [x] **T-066** — Render/system: centralize GPU stage probes via SystemManager TickObserver · Owner: claude/T-066-tick-observer · PR: https://github.com/jakildev/IrredenEngine/pull/401
- [x] **T-081** — Review-pr: detect oversized churn on CONFLICTING PRs + forked-from-other-PR signal · Owner: claude/T-081-review-pr-conflicting-churn · PR: https://github.com/jakildev/IrredenEngine/pull/400
- [x] **T-079** — Fleet: permissions and summaries-on-exit — .claude/commands/ writes, rm allowlist, restore non-architect summaries · Owner: claude/T-079-permissions-and-summaries · PR: https://github.com/jakildev/IrredenEngine/pull/398
- [x] **T-078** — Fleet: worktree contention — extend branch-lock filter, abort merger rebase on give-up, prevent parent-clone misroute · Owner: claude/T-078-worktree-contention · PR: https://github.com/jakildev/IrredenEngine/pull/397
- [x] **T-069** — Metal: port entity-id readback into f_trixel_to_framebuffer · Owner: claude/T-069-metal-entity-id-readback · PR: https://github.com/jakildev/IrredenEngine/pull/394
- [x] **T-065** — Render systems: migrate 12 files off function-local static onto SystemParams · Owner: claude/T-065-render-system-params · PR: https://github.com/jakildev/IrredenEngine/pull/382
- [x] **T-075** — Fleet docs: calibrate Opus-only review checklist + process gaps (Tier 2) · Owner: claude/T-075-opus-only-checklist · PR: https://github.com/jakildev/IrredenEngine/pull/395
- [x] **T-073** — ECS: support non-default-constructible component types in EntityManager::setComponent · Owner: claude/T-073-non-default-component · PR: https://github.com/jakildev/IrredenEngine/pull/392
- [x] **T-076** — Fleet docs: worker-doc process tweaks and tooling cleanup (Tier 3) · Owner: claude/T-076-worker-doc-tweaks · PR: https://github.com/jakildev/IrredenEngine/pull/391
