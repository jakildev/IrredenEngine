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
  - **Blocked by:** (none)
  - **Stack:** T-054..T-058 z-yaw-pipeline
  - **Acceptance:** (1) new GLSL + MSL shader pair committed under `engine/render/src/shaders/`; (2) new system registered at `SCREEN_SPACE_RESIDUAL_ROTATE` pipeline slot; (3) at `visualYaw=0°` the framebuffer is pixel-identical to canvas content (passthrough); (4) at `visualYaw=30°` with cardinal-snap raster from T-055, visible result rotates 30° in screen space without distortion beyond expected bilinear filtering; (5) `render-debug-loop` screenshots showing rotations at 0°, 30°, 60°, 90°, 120° demonstrating smooth continuity; (6) builds clean on `linux-debug` AND `macos-debug`
  - **Issue:** #322
  - **Notes:** Child of epic #310 (z-yaw-pipeline). Slots between `TRIXEL_TO_TRIXEL` and `FRAMEBUFFER_TO_SCREEN` in the render pipeline. Reads `residualYaw` from T-054. Uses bilinear filtering; pivot is canvas center. At `residualYaw=0`, must be pixel-identical (add explicit epsilon early-out if needed). Blocks T-057 (picking must invert this pass AND the cardinal raster). Full plan: `.fleet/plans/T-054.md`.
  - **Links:**

- [~] **Render/system: centralize GPU stage probes via SystemManager TickObserver** — add a generic `TickObserver` hook to `SystemManager`, implement `GpuStageTimingObserver` in render layer, and remove the 15+ inlined probe blocks from render system headers
  - **ID:** T-066
  - **Area:** engine/system, engine/render, engine/prefabs/irreden/render
  - **Model:** opus
  - **Owner:** claude/T-066-tick-observer
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
  - **Blocked by:** (none)
  - **Acceptance:** (1) SDF dispatch path has analytical fast paths at non-zero yaw OR the O(N) brute-force path is explicitly documented as intentional with the confusing multi-branch structure removed; (2) `smoothMode` gate restructured to eliminate redundancy; (3) snap-mode voxel-pool carving uses `rasterYaw` to align with cardinal-snap trixel raster from T-055; (4) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`; (5) `render-debug-loop` screenshots at yaw=0 (byte-identical to master), yaw=π/6, yaw=π/4, yaw=π/2 verify no coverage gaps
  - **Issue:** #345
  - **Notes:** Escalated from sonnet-author after human:needs-fix on PR #334 (T-056). Three human concerns: (1) confusing nesting + redundant smoothMode check, (2) no analytical fast path at non-zero yaw (existing `boxDepthIntersect` etc. bake in yaw=0 iso direction), (3) voxel-pool carving not rotating with T-055. Blocked until both T-055 (cardinal-snap raster) and T-056 (SDF arbitrary yaw) land. Opus architectural decision required on whether to rederive analytical intersectors for rotated ray.
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
  - **Blocked by:** T-070
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

- [~] **Fleet: orchestration calibration — babysit cooldown, fleet-claim git-aware, stale-status auto-flip** — enforce 30m floor between opus-reviewer relaunches; make fleet-claim resolve blockers via git merge-base; queue-manager detects stale [~] tasks merged to master and auto-flips to [x]
  - **ID:** T-080
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** claude/T-080-orchestration-calibration
  - **Blocked by:** (none)
  - **Acceptance:** (1) fleet-babysit enforces ~30m floor between opus-reviewer relaunches OR opus-reviewer does live `gh pr view <N> --json reviews` per candidate before reviewing; (2) `fleet-claim` blocker-check uses `git merge-base --is-ancestor <pred-branch-tip> origin/master` to resolve blockers independent of TASKS.md state; (3) queue-manager maintenance pass detects `[~]` tasks whose branch is an ancestor of master (empty diff) and auto-flips to `[x]` with merged-PR cross-reference; (4) script/doc changes; no engine build required
  - **Issue:** #387
  - **Notes:** Three issues: D2 opus-reviewer relaunched 2.4m after previous iteration (30m floor not enforced) burning ~5m context; D3 T-053 unclaimable 5–15m after T-051 merged because fleet-claim reads TASKS.md not git; D4 T-064 had stale `[~]` status after PR #383 merged — empty-diff PR #383 opened and immediately closed.
  - **Links:**

- [~] **Review-pr: detect oversized churn on CONFLICTING PRs + forked-from-other-PR signal** — add `gh pr diff --stat` check when mergeable==CONFLICTING; add fleet:fork-of-other-pr label when merger detects branch forked from another open PR
  - **ID:** T-081
  - **Area:** tooling, docs
  - **Model:** sonnet
  - **Owner:** claude/T-081-review-pr-conflicting-churn
  - **Blocked by:** (none)
  - **Acceptance:** (1) `review-pr/SKILL.md` runs `gh pr diff <N> --stat` when `mergeable == CONFLICTING`; flags files with >100 lines churn not in PR's claimed file list; (2) merger adds `fleet:fork-of-other-pr` label (or repurposes `fleet:awaiting-base` with updated description) when detecting fork condition; (3) opus-worker step 1c excludes `fleet:fork-of-other-pr` same as `fleet:awaiting-base`; (4) CLAUDE.md labeling section documents new label; doc + label creation; no engine build required
  - **Issue:** #388
  - **Notes:** Two issues: E1 PR #382 (T-065) would have silently reverted ~590 lines of PR #368 (lighting fidelity polish) if merged — the `mergeable: CONFLICTING` status was the signal; Sonnet missed it because no `gh pr diff --stat` check exists; E2 PR #378 (T-063) was forked from `claude/T-062-lambda-decay` branch (not master), carrying T-062's 10 commits — `fleet:semantic-conflict` set by merger was misleading; the right resolution is `git rebase --onto origin/master <T-062-tip> <T-063-branch>` once T-062 merges. Detect via `git merge-base PR-head other-PR-head == other-PR-head`.
  - **Links:**

- [~] **Fleet: factor CLAUDE.md status-prose sections to prevent parallel-PR rebase conflicts** — move rapidly-changing status prose from feature-PR-editable CLAUDE.md sections into queue-manager-owned file(s); update worker docs to restate only changed lines when editing shared status sections
  - **ID:** T-082
  - **Area:** tooling, docs
  - **Model:** opus
  - **Owner:** claude/T-082-status-prose-factor
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

- [x] **T-079** — Fleet: permissions and summaries-on-exit — .claude/commands/ writes, rm allowlist, restore non-architect summaries · Owner: claude/T-079-permissions-and-summaries · PR: https://github.com/jakildev/IrredenEngine/pull/398
- [x] **T-078** — Fleet: worktree contention — extend branch-lock filter, abort merger rebase on give-up, prevent parent-clone misroute · Owner: claude/T-078-worktree-contention · PR: https://github.com/jakildev/IrredenEngine/pull/397
- [x] **T-069** — Metal: port entity-id readback into f_trixel_to_framebuffer · Owner: claude/T-069-metal-entity-id-readback · PR: https://github.com/jakildev/IrredenEngine/pull/394
- [x] **T-065** — Render systems: migrate 12 files off function-local static onto SystemParams · Owner: claude/T-065-render-system-params · PR: https://github.com/jakildev/IrredenEngine/pull/382
- [x] **T-075** — Fleet docs: calibrate Opus-only review checklist + process gaps (Tier 2) · Owner: claude/T-075-opus-only-checklist · PR: https://github.com/jakildev/IrredenEngine/pull/395
- [x] **T-073** — ECS: support non-default-constructible component types in EntityManager::setComponent · Owner: claude/T-073-non-default-component · PR: https://github.com/jakildev/IrredenEngine/pull/392
- [x] **T-076** — Fleet docs: worker-doc process tweaks and tooling cleanup (Tier 3) · Owner: claude/T-076-worker-doc-tweaks · PR: https://github.com/jakildev/IrredenEngine/pull/391
- [x] **T-077** — Fleet: label discipline — verdict-without-label, has-nits stripping, changes-made handoff · Owner: claude/T-077-label-discipline · PR: https://github.com/jakildev/IrredenEngine/pull/393
- [x] **T-074** — Fleet docs: add silent-correctness rule coverage to review-pr + simplify (Tier 1) · Owner: claude/T-074-review-simplify-checks · PR: https://github.com/jakildev/IrredenEngine/pull/390
- [x] **T-056** — Render: SDF shape rasterization under arbitrary Z-yaw · Owner: claude/T-056-sdf-yaw · PR: https://github.com/jakildev/IrredenEngine/pull/334
- [x] **T-055** — Render: trixel rasterization under cardinal-snap Z-yaw · Owner: claude/T-055-trixel-cardinal-yaw · PR: https://github.com/jakildev/IrredenEngine/pull/333
- [x] **T-063** — Fleet: design-escalation flow — bidirectional labels + plan re-sync + role docs · Owner: claude/T-063-design-escalation · PR: https://github.com/jakildev/IrredenEngine/pull/378
- [x] **T-062** — Modifier framework: lambda decay system + stateful-lambda design · Owner: claude/T-062-lambda-decay · PR: https://github.com/jakildev/IrredenEngine/pull/351
- [x] **T-053** — Modifier framework: modifier_demo creation (visual showcase) · Owner: claude/T-053-modifier-demo · PR: https://github.com/jakildev/IrredenEngine/pull/377
- [x] **T-064** — engine/system docs: document 'no function-local static for system state' rule · Owner: claude/T-064-system-static-docs · PR: https://github.com/jakildev/IrredenEngine/pull/349
- [x] **T-060** — Modifier framework: wire MODIFIER_RESOLVE_EXEMPT via archetype exclude-tag filter · Owner: claude/T-060-exclude-tag-filter · PR: https://github.com/jakildev/IrredenEngine/pull/348
- [x] **T-061** — Modifier framework: pre-destroy hook for auto-sweep of source-attributed modifiers · Owner: claude/T-061-pre-destroy-hook · PR: https://github.com/jakildev/IrredenEngine/pull/347
- [x] **T-059** — Fleet docs: dormancy-verification rule for private creations · Owner: claude/T-059-dormancy-check · PR: https://github.com/jakildev/IrredenEngine/pull/346
- [x] **T-051** — Modifier framework: migrate position + velocity-drag patterns · Owner: claude/T-051-modifier-position-velocity · PR: https://github.com/jakildev/IrredenEngine/pull/332
- [x] **T-052** — Modifier framework: Lua bindings · Owner: claude/T-052-lua-bindings · PR: https://github.com/jakildev/IrredenEngine/pull/331
