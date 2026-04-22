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
  moving X out of the hot header"
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

- [ ] **Linux build maturation: get `linux-debug` preset green end-to-end** —
  fix every compile/link/runtime issue encountered when building the
  engine against the new `linux-debug` CMake preset inside WSL2 Ubuntu
  24.04. This is the umbrella epic — break it into smaller follow-up
  tasks as concrete issues are found.
  - **ID:** T-001
  - **Area:** engine/* (anywhere the Linux path breaks)
  - **Model:** opus (for anything touching core-engine invariants) /
    sonnet (for mechanical port fixes like missing `#include`, case-
    sensitive paths, EOL drift)
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** from a fresh WSL2 Ubuntu 24.04 clone,
    `cmake --preset linux-debug && cmake --build build -j$(nproc)`
    builds `IRShapeDebug`, `IRCreationDefault`, and `IrredenEngineTest`
    with zero warnings escalated to errors, and `IRShapeDebug` launches
    via WSLg without crashing on its first frame.
  - **Notes:** the engine was originally written against Windows +
    MSYS2. Expect missing Linux branches under `#ifdef _WIN32`, missing
    system libs (update the apt list in `docs/AGENT_FLEET_SETUP.md` §1a
    as you find them), case-sensitive include mismatches, and EOL
    drift. Every real fix should land as its own dedicated PR — do
    **not** bundle unrelated Linux-port fixes into feature work.
    Reference `docs/AGENT_FLEET_SETUP.md` §10 for the list of known
    first-time issues.
  - **Links:**

- [x] **Metal parity: port `c_voxel_visibility_compact.glsl` to MSL** —
  the voxel-visibility compaction pass (reduces visible-voxel indices
  into a dense buffer) has no Metal counterpart. Same skill flow.
  - **ID:** T-006
  - **Area:** engine/render/src/shaders/metal
  - **Model:** opus
  - **Owner:** metal-voxel-visibility-compact-port
  - **Blocked by:** (none)
  - **Acceptance:** `engine/render/src/shaders/metal/c_voxel_visibility_compact.metal`
    exists, matches the GLSL binding/uniform layout, `macos-debug`
    build is clean, and `IRShapeDebug` on Metal gives the same
    visible-voxel counts per frame as the OpenGL version.
  - **Notes:** compaction shaders rely on atomic operations — pay
    attention to the `atomicAdd` → `atomic_fetch_add_explicit`
    translation and the memory-order argument (use `relaxed` unless
    the GLSL source implies otherwise). See the cheatsheet in the
    `backend-parity` skill.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/227

- [~] **Wire up a `backend-parity` dry run** — use the new
  `backend-parity` skill end-to-end on a known-small parity gap
  (pick one of the three MSL port tasks above, ideally
  `c_shapes_to_trixel` as the least invasive). This is the equivalent
  of the "example" tasks below — the goal is to exercise the skill's
  flow and catch any workflow bugs before running it on real
  production gaps.
  - **ID:** T-007
  - **Area:** tooling
  - **Model:** opus
  - **Owner:** metal-finish-parity
  - **Blocked by:** (none)
  - **Acceptance:** one parity PR opened, reviewed by `review-pr`,
    merged. Any workflow bugs in the `backend-parity` skill itself
    are filed as follow-up `[sonnet]` tasks to fix the skill.
  - **Notes:** treat any skill failure as a skill bug, not a task
    failure. The point of this dry run is to uncover workflow
    issues, not to ship the parity port (that happens as a natural
    side effect).
  - **Links:**

- [x] **Lighting: flood-fill light propagation with colored light (Phase 3)** — BFS-propagate colored emissive-voxel light and skylight through the occupancy grid, outputting per-voxel RGB light levels to a 3D texture consumed by the lighting application pass
  - **ID:** T-014
  - **Area:** engine/render, shaders/glsl
  - **Model:** opus
  - **Owner:** render-flood-fill-lighting
  - **Blocked by:** (none)
  - **Acceptance:** (1) emissive voxels propagate colored light outward, blocked by solid voxels from occupancy grid; (2) skylight propagates top-down via columnar span lists; (3) per-voxel RGB light levels stored in 3D texture accessible from GPU; (4) incremental update on voxel place/remove — no full rebuild per frame; (5) render debug screenshot: torch placement creates visible light pool; (6) performance: initial build < 5ms, incremental < 1ms; (7) builds clean on active preset
  - **Issue:** #168
  - **Notes:** BFS on 6-connected voxel grid; take component-wise max from all incoming light. Skylight: O(height) sweep per column using columnar span lists from occupancy grid. Start with CPU BFS; profile and move to GPU wavefront BFS (ping-pong SSBOs) if needed. C_LightSource { Color emitColor; uint8_t radius; LightType type; } — emissive entities are seeds. Taxicab (L1) distance falloff is intentional aesthetic.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/232

- [x] **Lighting: fog of war render pass (Phase 5 engine side)** — render fog-of-war overlay from a 2D visibility texture with LOS ray casting against the occupancy grid, exploiting the fixed iso camera angle for efficient depth calculation
  - **ID:** T-016
  - **Area:** engine/render, shaders/glsl
  - **Model:** opus
  - **Owner:** render-fog-of-war-v1
  - **Blocked by:** (none)
  - **Acceptance:** (1) fog texture renders over scene, darkening fogged/unexplored areas; (2) LOS ray casting returns correct results against occupancy grid; (3) heightmap-aware: units cannot see over hills; (4) solid voxels block LOS, characters and small objects don't; (5) smooth fog edges via blur; (6) Lua API: setFogCell, getFogCell, castLOS, revealRadius, fadeExplored callable; (7) render debug screenshot: fog visible, LOS blocking correct; (8) fog update + render < 1ms for typical map sizes; (9) builds clean on active preset
  - **Issue:** #170
  - **Notes:** fixed isometric camera enables O(1)-per-ray LOS — exploit winning depth at the iso-projected angle, not naive 3D ray march. Fog texture: 2D R8 (unexplored=0, explored-fogged=128, visible=255). Heightmap-aware LOS via columnar span lists from occupancy grid. Fog in TRIXEL_TO_TRIXEL or dedicated FOG_TO_TRIXEL pass. Visible=no-op, explored=desaturate+darken, unexplored=black. Game-side integration: jakildev/irreden#21.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/238

- [x] **Skill: wire attach-screenshots into engine author roles and commit-and-push** — update `role-sonnet-author.md`, `role-opus-worker.md`, and `commit-and-push` skill to conditionally invoke `attach-screenshots` before committing visual changes
  - **ID:** T-019
  - **Area:** tooling, .claude/skills
  - **Model:** sonnet
  - **Owner:** skills-attach-screenshots-wiring
  - **Blocked by:** (none)
  - **Acceptance:** (1) `role-sonnet-author.md` and `role-opus-worker.md` invoke `attach-screenshots` before `optimize`/`commit-and-push` when diff touches `engine/render/`, `engine/prefabs/irreden/render/`, any `.glsl`/`.metal` shader file, or `creations/demos/*/src/`; (2) purely mechanical refactors and doc PRs do NOT invoke the skill; (3) `commit-and-push` detects render/visual files in diff and prompts worker to run `attach-screenshots` first if `docs/pr-screenshots/<branch>/` does not yet exist; (4) all engine author role files build/parse cleanly; (5) at least one manually-verified run shows screenshots landing in the PR
  - **Issue:** #186
  - **Notes:** conditional trigger logic must be precise — only visual/render diffs trigger it. Mechanical refactors (rename, extract header) must not trigger it. `attach-screenshots` runs BEFORE `optimize` and `commit-and-push` so screenshots are in the same commit batch.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/225

- [x] **Migrate from one-PR-multi-commit stacks to true stacked PRs** — redesign the `fleet-claim stack` flow so each task in a chain becomes its own PR with a chained `--base`, enabling independent per-task review and merge
  - **ID:** T-020
  - **Area:** tooling, .claude/skills
  - **Model:** opus
  - **Owner:** stacked-prs-reviewer-alignment
  - **Blocked by:** (none)
  - **Acceptance:** (1) worker can run `fleet-claim stack "T-A T-B T-C"` and the result is 3 chained PRs (PR-A base=master, PR-B base=PR-A, PR-C base=PR-B); (2) reviewers see and review each PR independently with its own `fleet:approved`/`fleet:needs-fix` label; (3) when PR-A merges, PR-B's base auto-rebases to master without manual intervention (or worker is instructed to rebase as a follow-up); (4) queue-manager correctly handles the cascade: PR-A merge → T-A done, PR-B merge → T-B done, etc.; (5) at least one stack of 3 tasks shipped end-to-end via this flow with no manual intervention
  - **Issue:** #183
  - **Notes:** explicitly [opus] — touches fleet-claim, role files, requires reasoning about edge cases (rebase failures, mid-chain rejections, partial approvals). Current one-PR-multi-commit approach (PR #182) stays in place until this lands. Subcomponents: worker creates chained branches + PRs; queue-manager detects chained PRs and handles cascade; fleet-claim tracks chain state in `~/.fleet/claims/_stack_<agent>/prs`; reviewer notes parent PRs in review. Filed from PR #182; related: jakildev/IrredenEngine#175.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/254

- [x] **Fleet: resumable workflows (molecules) for stacked task chains** — extend `fleet-claim stack` to write a molecule YAML that survives agent crashes, allowing the next worker startup to auto-resume the in-progress step
  - **ID:** T-021
  - **Area:** tooling, .claude/skills
  - **Model:** opus
  - **Owner:** fleet-resumable-molecules
  - **Blocked by:** (none)
  - **Acceptance:** (1) `fleet-claim stack` creates `~/.fleet/molecules/<name>.yml` with per-task state (pending/in-progress/done/failed); (2) worker crashes mid-stack (kill process during T-002) — on restart, worker reads molecule, resumes or restarts T-002, then completes T-003 without human intervention; (3) `fleet-claim molecule list/resume/advance/complete` commands work correctly; (4) molecule files survive `fleet-up` restarts and are NOT wiped by `fleet-claim clear-all`; (5) worker role files document "check molecule on startup before picking new work" as highest-priority step; (6) at least one real stack crashed-and-resumed in testing
  - **Issue:** #191
  - **Notes:** explicitly [opus] — crash-recovery edge cases (resume vs restart judgment) require real reasoning. Builds on fleet-claim stack from #177. Particularly valuable before lighting batch (T-010…T-016) starts in earnest. Molecule format: yaml with name, agent, created, tasks list (id + state + pr + commit). Inspiration: gas town Molecules concept.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/230

- [x] **Fleet: witness health monitoring with heartbeat detection** — add a `witness` role and per-agent heartbeat writes so hung agents (alive but stuck) are detected within 60s and surfaced as alerts
  - **ID:** T-023
  - **Area:** tooling, .claude/skills
  - **Model:** sonnet
  - **Owner:** fleet-witness-heartbeat
  - **Blocked by:** (none)
  - **Acceptance:** (1) each agent role file writes `date -u` to `~/.fleet/heartbeats/<agent-name>` every loop iteration (and at major checkpoints for long tasks); (2) `~/.fleet/heartbeats/` populates during normal operation; (3) witness pane in `fleet-up`, polls every 60s; (4) killing an agent with `kill -STOP` triggers a stale-heartbeat log entry within 60s of crossing the per-agent threshold; (5) healthy fleet shows "all healthy (N agents tracked)" with no false positives during normal long-running work; (6) opus-architect (interactive, no loop) is excluded from monitoring; (7) stale alerts written to `~/.fleet/alerts/<agent>.stuck`
  - **Issue:** #193
  - **Notes:** explicitly [sonnet] — pure file-polling and timestamp comparison, no judgment calls. Per-agent thresholds: sonnet-author ≤5m, sonnet-reviewer ≤6m, queue-manager ≤10m, opus-worker ≤30m, opus-reviewer ≤45m. v1 alerts only — no auto-interrupt (too risky). Inspiration: gas town Witness role.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/229

- [x] **Lighting: culling invariants — document off-screen-caster, shadow-ring, light-seed expansion, and AO guard band** — add a canonical "Lighting culling invariants" section to `engine/render/CLAUDE.md` covering the four invariants that downstream lighting PRs (AO, shadows, flood-fill, fog-of-war) must respect to handle off-screen geometry correctly
  - **ID:** T-024
  - **Area:** docs, engine/render
  - **Model:** sonnet
  - **Owner:** docs-lighting-culling-invariants
  - **Blocked by:** (none)
  - **Acceptance:** (1) new section "Lighting culling invariants" added to `engine/render/CLAUDE.md` containing all four invariants (grid-build iterates full pool not render-culled subset; shadow-ring extent formula when chunk streaming activates; flood-fill seed from all C_LightSource entities within frustum + max(radius) expansion; 1-chunk AO/shadow guard band around view-chunk set); (2) issues #166 (AO), #167 (shadows), #168 (flood-fill), #170 (fog-of-war) cross-linked to issue #196 via comments; (3) T-010 occupancy-grid-build system (already merged in PR #188) checked against invariant #1 — if violated, a follow-up task is filed; if compliant, that is noted in the PR/issue; (4) builds clean on active preset (doc-only change)
  - **Issue:** #196
  - **Notes:** explicitly [sonnet] per issue — this is a write-up and cross-linking task, not a design decision. T-010 (#164) already merged via PR #188; invariant #1 check applies to the merged form. The four invariants were specified by the opus-architect during C_LightSource/C_LightBlocker review (PR #187). Escalate to [opus] only if invariant #1 is found violated in T-010 and structural fixes are needed.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/234

- [x] **Render debug: false-color lighting-data overlay** — add a Lua-configurable debug mode that replaces the artistic lighting output with a false-color visualization of a selected lighting buffer (AO, light level, shadow)
  - **ID:** T-025
  - **Area:** engine/render, shaders/glsl, shaders/metal, engine/script
  - **Model:** opus
  - **Owner:** render-debug-overlay
  - **Blocked by:** (none)
  - **Acceptance:** (1) `ir.render.setDebugOverlay("ao"|"light_level"|"shadow"|"none")` Lua setter exposed; (2) at least three modes implemented: `ao`, `light_level`, `shadow`; (3) switching modes at runtime works on both backends (GLSL + MSL parity); (4) `IRShapeDebug` demonstrates each mode via Lua config or CLI flag; (5) documented in `engine/render/CLAUDE.md` as recommended sanity-check for lighting work; (6) builds clean on `linux-debug` and `macos-debug`
  - **Issue:** #218
  - **Notes:** AO texture already available from T-012 (PR #197). Extends stub `f_debug_overlay.glsl` and `engine/render/src/shaders/metal/debug_overlay.metal`. Debug overlay pass runs after lighting passes but replaces final composited output in debug mode — artistic LIGHTING_TO_TRIXEL result still computed but discarded. AO viz: `vec3(1-ao, ao, 0)` (red=occluded, green=clear). Light level: `vec3(level, level, 1.0)` (blue→white). Shadow: black/magenta. Soft suggestion: pair shadow debug mode with T-013 landing. Mirror pattern of `setSubdivisionMode` in render API.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/235

- [x] **Render verification: reference-image comparison harness** — build a `/render-verify` skill that captures screenshots via `--auto-screenshot`, compares them against committed reference PNGs, and reports pass/fail with diff images
  - **ID:** T-026
  - **Area:** tooling, .claude/skills, docs
  - **Model:** opus
  - **Owner:** render-verify-harness
  - **Blocked by:** (none)
  - **Acceptance:** (1) reference library structure decided and documented (path, naming, per-shot layout); (2) `scripts/render-compare.py` (or equivalent) exists and produces pass/fail + diff image on mismatches; (3) `.claude/skills/render-verify/SKILL.md` wraps build → run → capture → compare → report flow; (4) `IRShapeDebug` reference set committed; skill produces all-pass on clean master; (5) builds + skill run cleanly on `linux-debug`
  - **Issue:** #217
  - **Notes:** soft dependency on T-027 (#189, "Promote --auto-screenshot shot config into reusable engine helper") — without it, harness only verifies IRShapeDebug; with it, any demo that opts in works. Can ship for IRShapeDebug first. Comparison algorithm choice (pixel diff / PSNR / perceptual hash) is the key design decision — threshold knob matters more than algorithm. Related: PR #190 (merged) provides render-debug-loop that this wraps. Companion to #218 (debug overlay, different concern: in-flight debugging vs post-hoc regression).
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/233

- [x] **Promote --auto-screenshot into a reusable engine helper** — extract the hand-written shot-cycling machinery from `shape_debug` into a declarative `IRVideo::enableAutoScreenshot()` API so any creation can opt in with a shot list
  - **ID:** T-027
  - **Area:** engine/video, creations/demos/shape_debug
  - **Model:** opus
  - **Owner:** engine-video-auto-screenshot-helper
  - **Blocked by:** (none)
  - **Acceptance:** (1) engine exposes `AutoScreenshotShot`, `AutoScreenshotConfig`, and `enableAutoScreenshot()` (or equivalent declarative API); (2) `shape_debug/main.cpp` ported to helper — hand-written `ShotConfig`/`g_shots`/`AutoScreenshot` system deleted, net line count goes down; (3) `fleet-run IRShapeDebug --auto-screenshot 10` produces same screenshot set as before; (4) a second creation opts into helper with a 3-shot table and produces 3 screenshots (proof-of-reuse gate); (5) `.claude/skills/render-debug-loop/SKILL.md` updated to point at helper; (6) builds clean on `linux-debug`
  - **Issue:** #189
  - **Notes:** reference implementation to be promoted is `creations/demos/shape_debug/main.cpp` (~80 lines: ShotConfig, g_shots[], warmup/settle counters, AutoScreenshot render-pipeline system, CLI arg parsing). Shot struct needs: zoom, cameraIso, subdivisionMode, label. Engine only owns the cycling mechanism — every creation defines its own shot table. Keep the shot struct extensible (possible `std::function<void()> preShot` callback) but start with zoom/camera/mode. Soft dependency: T-026 (render-verify harness) expands cleanly once this lands.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/228

- [x] **GPU timer query infrastructure (Part 1)** — add per-pass GPU timer queries to the render pipeline and expose pass timings via a Lua API; foundation for the `optimize` skill and lighting-phase perf validation
  - **ID:** T-028
  - **Area:** engine/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** render-gpu-timer-queries
  - **Blocked by:** (none)
  - **Acceptance:** (1) per-pass GPU timer queries (`GL_TIME_ELAPSED` or `GL_TIMESTAMP`) instrument each render pipeline stage; (2) `ir.render.getPassTimings()` Lua API returns per-pass millisecond breakdown; (3) frame-time budget reporting defines 16.6ms (60fps) target and flags passes exceeding allocated share; (4) timer infrastructure is globally enable/disable (default off in release); (5) `optimize` skill's "GPU profiling" section updated to use this API; (6) builds clean on active preset
  - **Issue:** #173
  - **Notes:** Part 2 (benchmark harness + render-perf skill, [sonnet]) depends on Part 1's Lua API and should be filed as a separate issue+task once Part 1 merges — do not bundle both into one PR. Metal parity for timer infrastructure is a follow-up. OpenGL bindings (`glQueryCounter`/`GL_TIMESTAMP`) already exist in `engine/render/include/glad/glad.h` — no new GL extension needed. T-013 (sun shadows) and T-014 (flood-fill) are the first lighting phases that will need perf validation.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/237

- [~] **Fleet: cross-host smoke-test running-tally for render changes** — add host-validation labels so render/shader PRs that land on one backend are automatically tracked until validated on the lagging host
  - **ID:** T-029
  - **Area:** tooling
  - **Model:** opus
  - **Owner:** T-029-cross-host-smoke-tally
  - **Blocked by:** (none)
  - **Acceptance:** (1) labels `fleet:needs-linux-smoke` and `fleet:needs-macos-smoke` are auto-applied (by reviewer or queue-manager) to engine PRs touching `engine/render/`, `shaders/`, or `engine/prefabs/irreden/render/` that were authored on only one host; (2) when a fleet starts on the lagging host, agents pick up tagged PRs and run build + `render-verify` (or a targeted smoke test); (3) after validation, agent removes the label and posts a confirmation comment; (4) no open render PRs slip through without both-host validation before merge; (5) skill or role doc updated to document the tagging trigger and resolution flow
  - **Issue:** #250
  - **Notes:** explicitly [opus] per issue — involves reviewer label-application logic, fleet startup scanning, and render-verify integration. v1: manual tag by reviewer is acceptable; auto-detection from diff can come later. Related: `backend-parity` skill covers structural GLSL↔MSL porting; this covers runtime-smoke validation after a port lands.
  - **Links:**

- [x] **Fleet: review-pr verifies previously-flagged hunks before re-checklist** — add a re-review sub-step that reads prior review comments and confirms each flagged hunk is actually still present at HEAD before re-running the full checklist
  - **ID:** T-030
  - **Area:** tooling, .claude/skills
  - **Model:** sonnet
  - **Owner:** skills-review-pr-hunk-verify
  - **Blocked by:** (none)
  - **Acceptance:** (1) `review-pr/SKILL.md` re-review path fetches the prior review body and parses flagged file:line references from Blockers/Needs-fix/Nits sections; (2) for each flagged hunk, reads the file at HEAD and confirms whether the issue is still present; (3) new review explicitly states "verified fixed at <SHA>" for each resolved nit; (4) full fresh-eyes checklist runs only after prior-review verification step; (5) no false-positive re-flags of already-fixed issues in a manually-triggered re-review
  - **Issue:** #251
  - **Notes:** explicitly [sonnet] per issue — bounded skill-file edit, no judgment calls on review substance. Root cause: re-reviewer re-ran full checklist without cross-checking author's fix commit, resulting in a hallucinated "still present" finding. Related skill: `request-re-review/SKILL.md`, `review-pr/SKILL.md` re-review section (~lines 246-258).
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/258

- [x] **Fleet: commit-and-push post-rebase self-diff to catch silently-dropped hunks** — capture a pre-rebase diff snapshot and compare it against the post-rebase state to surface any hunks git silently dropped during 3-way merge resolution
  - **ID:** T-031
  - **Area:** tooling, .claude/skills
  - **Model:** sonnet
  - **Owner:** skills-commit-push-prerebase-diff
  - **Blocked by:** (none)
  - **Acceptance:** (1) `commit-and-push/SKILL.md` captures `git diff origin/master` to `/tmp/fleet-prerebase.diff` before the rebase starts; (2) after rebase + conflict resolution, captures post-rebase diff and diffs the two; (3) any line present in pre-rebase but absent in post-rebase is surfaced to the agent before commit; (4) if the rebase was done without pre-capture, `commit-and-push` checks `git reflog` for a recent rebase and warns; (5) same pre/post check added to `merger` skill's conflict-resolution path
  - **Issue:** #252
  - **Notes:** explicitly [sonnet] per issue — bounded skill-file edit. Root cause: git's 3-way merge silently dropped two `else { applyCheckerboard(...) }` branches from a non-conflicting section of a file that had a conflict elsewhere during the PR #238 rebase. Silent hunk loss is the most insidious rebase failure mode — the check is a commit-time safety net. Pre-capture must happen BEFORE the rebase or the guard cannot function retroactively.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/259

- [ ] **Remove engine-side midi_polyrhythm demo after game port lands** — delete `creations/demos/midi_polyrhythm/` from the engine repo and remove its CMake subdirectory entry to eliminate the duplicate build target
  - **ID:** T-032
  - **Area:** build, creations/demos
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `creations/demos/midi_polyrhythm/` directory completely removed from engine repo; (2) `add_subdirectory` for midi_polyrhythm removed from `creations/CMakeLists.txt`; (3) engine build (`fleet-build --target IRShapeDebug` or any non-midi target) compiles clean with no broken references; (4) no dangling references to midi_polyrhythm in any remaining CMake files
  - **Issue:** #255
  - **Notes:** [sonnet] per issue — mechanical deletion plus smoke-test build. Must NOT be merged before the game-side midi port PR has merged (the port moves the creation to the game repo; if the engine copy disappears first, the IRMidiPolyrhythm target vanishes entirely until the game PR lands). Worker should verify the game port is live before opening this PR.
  - **Links:**

---

## In progress

<!-- Tasks currently being worked on. Mirror of [~] items above. -->

- [~] **T-007** — Wire up a `backend-parity` dry run · Owner: metal-finish-parity · PR: https://github.com/jakildev/IrredenEngine/pull/260
- [~] **T-029** — Fleet: cross-host smoke-test running-tally for render changes · Owner: T-029-cross-host-smoke-tally · PR: https://github.com/jakildev/IrredenEngine/pull/262

---

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-016** — Lighting: fog of war render pass (Phase 5 engine side) · Owner: render-fog-of-war-v1 · PR: https://github.com/jakildev/IrredenEngine/pull/238
- [x] **T-025** — Render debug: false-color lighting-data overlay · Owner: render-debug-overlay · PR: https://github.com/jakildev/IrredenEngine/pull/235
- [x] **T-031** — Fleet: commit-and-push post-rebase hunk-loss guard · Owner: skills-commit-push-prerebase-diff · PR: https://github.com/jakildev/IrredenEngine/pull/259
- [x] **T-030** — Fleet: review-pr verifies previously-flagged hunks on re-review · Owner: skills-review-pr-hunk-verify · PR: https://github.com/jakildev/IrredenEngine/pull/258
- [x] **T-028** — GPU timer query infrastructure (Part 1) · Owner: render-gpu-timer-queries · PR: https://github.com/jakildev/IrredenEngine/pull/237
- [x] **T-020** — Migrate from one-PR-multi-commit stacks to true stacked PRs · Owner: stacked-prs-reviewer-alignment · PR: https://github.com/jakildev/IrredenEngine/pull/254
- [x] **T-026** — Render verification: reference-image comparison harness · Owner: render-verify-harness · PR: https://github.com/jakildev/IrredenEngine/pull/233
- [x] **T-024** — Lighting: culling invariants doc · Owner: docs-lighting-culling-invariants · PR: https://github.com/jakildev/IrredenEngine/pull/234
- [x] **T-014** — Lighting: flood-fill light propagation with colored light (Phase 3) · Owner: render-flood-fill-lighting · PR: https://github.com/jakildev/IrredenEngine/pull/232
- [x] **T-021** — Fleet: resumable workflows (molecules) for stacked task chains · Owner: fleet-resumable-molecules · PR: https://github.com/jakildev/IrredenEngine/pull/230
- [x] **T-023** — Fleet: witness health monitoring with heartbeat detection · Owner: fleet-witness-heartbeat · PR: https://github.com/jakildev/IrredenEngine/pull/229
- [x] **T-027** — Promote --auto-screenshot into a reusable engine helper · Owner: engine-video-auto-screenshot-helper · PR: https://github.com/jakildev/IrredenEngine/pull/228
- [x] **T-006** — Metal parity: port c_voxel_visibility_compact.glsl to MSL · Owner: metal-voxel-visibility-compact-port · PR: https://github.com/jakildev/IrredenEngine/pull/227
- [x] **T-019** — Skill: wire attach-screenshots into engine author roles and commit-and-push · Owner: skills-attach-screenshots-wiring · PR: https://github.com/jakildev/IrredenEngine/pull/225

- [x] **Fleet: merger orchestrator pane for auto-resolving PR conflicts** — add a `merger` role that polls for conflicting PRs, auto-resolves mechanical conflicts (TASKS.md sort-merge, whitespace, clean-rebase), and labels non-mechanical conflicts for human
  - **ID:** T-022
  - **Area:** tooling, .claude/skills
  - **Model:** opus
  - **Owner:** fleet-merger-orchestrator
  - **Blocked by:** (none)
  - **Acceptance:** (1) merger pane in `fleet-up`, polls every 10m; (2) two PRs touching same file far apart get auto-rebased and emerge MERGEABLE; (3) TASKS.md conflicts (two PRs adding different tasks) auto-resolved by sort-merge; (4) non-mechanical conflicts (same code lines changed differently) labeled `human:needs-fix` with conflict description — NEVER auto-resolved; (5) merger never force-pushes to master, never calls `gh pr merge`; (6) every action logged to `~/.fleet/logs/merger.log` and posted as PR comment; (7) PRs labeled `human:wip`, `fleet:wip`, or `fleet:blocker` are skipped
  - **Issue:** #192
  - **Notes:** explicitly [opus] — conflict classification (mechanical vs semantic) requires judgment. v1 scope: TASKS.md sort-merge, whitespace-only, clean-rebase only. More heuristics added incrementally. Uses `--force-with-lease` not `--force`. Inspiration: gas town Refinery role.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/224

- [x] **Lighting: directional sun shadows via shadow height map (Phase 2)** — sweep the 3D occupancy grid along the sun direction to build a shadow height map, make shadows visible at runtime as the sun direction changes
  - **ID:** T-013
  - **Area:** engine/render, shaders/glsl
  - **Model:** opus
  - **Owner:** render-shadow-map-phase2
  - **Blocked by:** (none)
  - **Acceptance:** (1) directional shadows visible — buildings cast shadows on ground, terrain creates shade; (2) overhangs and caves correctly shadowed (columnar span lists, not just heightmap); (3) sun direction changeable at runtime with shadow map rebuilding within one frame; (4) render debug screenshots at multiple sun angles; (5) shadow map rebuild < 1ms for typical world sizes; (6) builds clean on active preset
  - **Issue:** #167
  - **Notes:** shadow height map sweep: `S(x,z) = max(H(x,z), S(x-1,z) - slope)` — O(N) pass over column grid. Fixed iso camera maps shadow direction to constant screen-space offset. Use columnar span lists for overhangs (not just heightmap). Sun direction stored as world-space unit vector; rebuilds triggered on fixed angular steps. Output: 2D shadow texture in iso-space (or 3D shadow volume for overhangs). Soft shadows optional — start with hard, soften later. Blocked by #164 + #165.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/210

- [x] **Metal parity: port `c_update_voxel_positions.glsl` to MSL** — GPU-side voxel-position update compute ported to Metal.
  - **ID:** T-005
  - **Area:** engine/render/src/shaders/metal
  - **Model:** opus
  - **Owner:** metal-update-voxel-positions-port
  - **Blocked by:** (none)
  - **Acceptance:** `.metal` shader exists, `macos-debug` build clean, voxel animation identical to OpenGL.
  - **Issue:** (none)
  - **Notes:** (none)
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/223

- [x] **Metal parity: port `c_shapes_to_trixel.glsl` to MSL** — GLSL compute for shape SDFs into trixel canvases ported to Metal.
  - **ID:** T-004
  - **Area:** engine/render/src/shaders/metal
  - **Model:** opus
  - **Owner:** metal-shapes-to-trixel-port
  - **Blocked by:** (none)
  - **Acceptance:** `.metal` shader exists, `macos-debug` build clean, shapes render identically to OpenGL.
  - **Issue:** (none)
  - **Notes:** (none)
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/222

- [x] **Example: benchmark IRShapeDebug at zoom 4** — measure per-system timing and file a report.
  - **ID:** T-008
  - **Area:** engine/profile
  - **Model:** opus
  - **Owner:** perf-shape-debug-zoom4
  - **Blocked by:** (none)
  - **Acceptance:** `docs/perf-reports/shape_debug_zoom4.md` exists with per-system `easy_profiler` screenshots and a 1-paragraph summary of the top 3 hotspots.
  - **Notes:** use `IRShapeDebug` from `creations/demos/shape_debug/`. See `engine/profile/CLAUDE.md` for the macros and how to enable the profiler build flag.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/220

- [x] **Lighting: LUT palette shading (Phase 4)** — replace linear brightness multiplication with an artist-authored palette LUT texture that maps (light level, hue) to final RGB, enabling cel-shading and stylized lighting
  - **ID:** T-015
  - **Area:** engine/render, shaders/glsl
  - **Model:** sonnet
  - **Owner:** render-lut-palette-shading
  - **Blocked by:** (none)
  - **Acceptance:** (1) palette LUT texture loaded and bound to lighting application pass; (2) different base hues produce distinct shadow/highlight colors; (3) GL_NEAREST mode produces clean cel-shading bands; (4) GL_LINEAR mode produces smooth gradients; (5) without LUT, falls back to linear multiply — no regression; (6) render debug screenshot: side-by-side linear vs LUT; (7) builds clean on active preset
  - **Issue:** #169
  - **Notes:** LUT is 2D PNG (256×16): X = light level 0–1, Y = palette row by hue/material. Total ~16KB. Default LUT ships with engine; games override. Also requires at least one of T-012, T-013, or T-014 producing light level data to see the effect; the LUT mechanism itself only requires T-011.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/198


