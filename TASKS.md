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

- [ ] **Metal parity: port `c_shapes_to_trixel.glsl` to MSL** —
  the GLSL compute for writing 2D shape SDFs into trixel canvases has
  no Metal counterpart. Invoke the `backend-parity` skill on a macOS
  host and port it.
  - **ID:** T-004
  - **Area:** engine/render/src/shaders/metal
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** `engine/render/src/shaders/metal/c_shapes_to_trixel.metal`
    exists and matches the GLSL binding/uniform layout, the
    `macos-debug` build is clean, and the shapes-rendering demo
    (whichever creation exercises `SHAPES_TO_TRIXEL`) renders
    visually identically to the OpenGL version.
  - **Notes:** read `engine/render/CLAUDE.md` pipeline overview and
    the GLSL source in full before porting. Use the cheatsheet in
    `.claude/skills/backend-parity/SKILL.md`.
  - **Links:**

- [ ] **Metal parity: port `c_update_voxel_positions.glsl` to MSL** —
  GPU-side voxel-position update compute with no Metal counterpart.
  Same skill flow as above.
  - **ID:** T-005
  - **Area:** engine/render/src/shaders/metal
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** `engine/render/src/shaders/metal/c_update_voxel_positions.metal`
    exists and matches the GLSL binding/uniform layout, `macos-debug`
    build is clean, and a voxel-animation demo (e.g. one of the
    `creations/demos/*` that exercises moving voxels) animates
    identically to the OpenGL version.
  - **Notes:** dispatch size and buffer bindings must match exactly
    — any mismatch will race or corrupt the voxel pool. This one is
    higher-stakes than `c_shapes_to_trixel`; be thorough.
  - **Links:**

- [ ] **Metal parity: port `c_voxel_visibility_compact.glsl` to MSL** —
  the voxel-visibility compaction pass (reduces visible-voxel indices
  into a dense buffer) has no Metal counterpart. Same skill flow.
  - **ID:** T-006
  - **Area:** engine/render/src/shaders/metal
  - **Model:** opus
  - **Owner:** free
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
  - **Links:**

- [ ] **Wire up a `backend-parity` dry run** — use the new
  `backend-parity` skill end-to-end on a known-small parity gap
  (pick one of the three MSL port tasks above, ideally
  `c_shapes_to_trixel` as the least invasive). This is the equivalent
  of the "example" tasks below — the goal is to exercise the skill's
  flow and catch any workflow bugs before running it on real
  production gaps.
  - **ID:** T-007
  - **Area:** tooling
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** one parity PR opened, reviewed by `review-pr`,
    merged. Any workflow bugs in the `backend-parity` skill itself
    are filed as follow-up `[sonnet]` tasks to fix the skill.
  - **Notes:** treat any skill failure as a skill bug, not a task
    failure. The point of this dry run is to uncover workflow
    issues, not to ship the parity port (that happens as a natural
    side effect).
  - **Links:**

- [ ] **Example: benchmark IRShapeDebug at zoom 4** — measure per-system
  timing and file a report.
  - **ID:** T-008
  - **Area:** engine/profile
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-001 (if running in the fleet;
    the Windows-native clone can start this immediately)
  - **Acceptance:** `docs/perf-reports/shape_debug_zoom4.md` exists with
    per-system `easy_profiler` screenshots and a 1-paragraph summary of the
    top 3 hotspots.
  - **Notes:** use `IRShapeDebug` from `creations/demos/shape_debug/`. See
    `engine/profile/CLAUDE.md` for the macros and how to enable the
    profiler build flag.
  - **Links:**

- [ ] **Lighting: directional sun shadows via shadow height map (Phase 2)** — sweep the 3D occupancy grid along the sun direction to build a shadow height map, make shadows visible at runtime as the sun direction changes
  - **ID:** T-013
  - **Area:** engine/render, shaders/glsl
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) directional shadows visible — buildings cast shadows on ground, terrain creates shade; (2) overhangs and caves correctly shadowed (columnar span lists, not just heightmap); (3) sun direction changeable at runtime with shadow map rebuilding within one frame; (4) render debug screenshots at multiple sun angles; (5) shadow map rebuild < 1ms for typical world sizes; (6) builds clean on active preset
  - **Issue:** #167
  - **Notes:** shadow height map sweep: `S(x,z) = max(H(x,z), S(x-1,z) - slope)` — O(N) pass over column grid. Fixed iso camera maps shadow direction to constant screen-space offset. Use columnar span lists for overhangs (not just heightmap). Sun direction stored as world-space unit vector; rebuilds triggered on fixed angular steps. Output: 2D shadow texture in iso-space (or 3D shadow volume for overhangs). Soft shadows optional — start with hard, soften later. Blocked by #164 + #165.
  - **Links:**

- [ ] **Lighting: flood-fill light propagation with colored light (Phase 3)** — BFS-propagate colored emissive-voxel light and skylight through the occupancy grid, outputting per-voxel RGB light levels to a 3D texture consumed by the lighting application pass
  - **ID:** T-014
  - **Area:** engine/render, shaders/glsl
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) emissive voxels propagate colored light outward, blocked by solid voxels from occupancy grid; (2) skylight propagates top-down via columnar span lists; (3) per-voxel RGB light levels stored in 3D texture accessible from GPU; (4) incremental update on voxel place/remove — no full rebuild per frame; (5) render debug screenshot: torch placement creates visible light pool; (6) performance: initial build < 5ms, incremental < 1ms; (7) builds clean on active preset
  - **Issue:** #168
  - **Notes:** BFS on 6-connected voxel grid; take component-wise max from all incoming light. Skylight: O(height) sweep per column using columnar span lists from occupancy grid. Start with CPU BFS; profile and move to GPU wavefront BFS (ping-pong SSBOs) if needed. C_LightSource { Color emitColor; uint8_t radius; LightType type; } — emissive entities are seeds. Taxicab (L1) distance falloff is intentional aesthetic.
  - **Links:**

- [~] **Lighting: LUT palette shading (Phase 4)** — replace linear brightness multiplication with an artist-authored palette LUT texture that maps (light level, hue) to final RGB, enabling cel-shading and stylized lighting
  - **ID:** T-015
  - **Area:** engine/render, shaders/glsl
  - **Model:** sonnet
  - **Owner:** render-lut-palette-shading
  - **Blocked by:** (none)
  - **Acceptance:** (1) palette LUT texture loaded and bound to lighting application pass; (2) different base hues produce distinct shadow/highlight colors; (3) GL_NEAREST mode produces clean cel-shading bands; (4) GL_LINEAR mode produces smooth gradients; (5) without LUT, falls back to linear multiply — no regression; (6) render debug screenshot: side-by-side linear vs LUT; (7) builds clean on active preset
  - **Issue:** #169
  - **Notes:** LUT is 2D PNG (256×16): X = light level 0–1, Y = palette row by hue/material. Total ~16KB. Default LUT ships with engine; games override. Also requires at least one of T-012, T-013, or T-014 producing light level data to see the effect; the LUT mechanism itself only requires T-011.
  - **Links:**

- [ ] **Lighting: fog of war render pass (Phase 5 engine side)** — render fog-of-war overlay from a 2D visibility texture with LOS ray casting against the occupancy grid, exploiting the fixed iso camera angle for efficient depth calculation
  - **ID:** T-016
  - **Area:** engine/render, shaders/glsl
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) fog texture renders over scene, darkening fogged/unexplored areas; (2) LOS ray casting returns correct results against occupancy grid; (3) heightmap-aware: units cannot see over hills; (4) solid voxels block LOS, characters and small objects don't; (5) smooth fog edges via blur; (6) Lua API: setFogCell, getFogCell, castLOS, revealRadius, fadeExplored callable; (7) render debug screenshot: fog visible, LOS blocking correct; (8) fog update + render < 1ms for typical map sizes; (9) builds clean on active preset
  - **Issue:** #170
  - **Notes:** fixed isometric camera enables O(1)-per-ray LOS — exploit winning depth at the iso-projected angle, not naive 3D ray march. Fog texture: 2D R8 (unexplored=0, explored-fogged=128, visible=255). Heightmap-aware LOS via columnar span lists from occupancy grid. Fog in TRIXEL_TO_TRIXEL or dedicated FOG_TO_TRIXEL pass. Visible=no-op, explored=desaturate+darken, unexplored=black. Game-side integration: jakildev/irreden#21.
  - **Links:**

- [ ] **Skill: wire attach-screenshots into engine author roles and commit-and-push** — update `role-sonnet-author.md`, `role-opus-worker.md`, and `commit-and-push` skill to conditionally invoke `attach-screenshots` before committing visual changes
  - **ID:** T-019
  - **Area:** tooling, .claude/skills
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `role-sonnet-author.md` and `role-opus-worker.md` invoke `attach-screenshots` before `optimize`/`commit-and-push` when diff touches `engine/render/`, `engine/prefabs/irreden/render/`, any `.glsl`/`.metal` shader file, or `creations/demos/*/src/`; (2) purely mechanical refactors and doc PRs do NOT invoke the skill; (3) `commit-and-push` detects render/visual files in diff and prompts worker to run `attach-screenshots` first if `docs/pr-screenshots/<branch>/` does not yet exist; (4) all engine author role files build/parse cleanly; (5) at least one manually-verified run shows screenshots landing in the PR
  - **Issue:** #186
  - **Notes:** conditional trigger logic must be precise — only visual/render diffs trigger it. Mechanical refactors (rename, extract header) must not trigger it. `attach-screenshots` runs BEFORE `optimize` and `commit-and-push` so screenshots are in the same commit batch.
  - **Links:**

- [ ] **Migrate from one-PR-multi-commit stacks to true stacked PRs** — redesign the `fleet-claim stack` flow so each task in a chain becomes its own PR with a chained `--base`, enabling independent per-task review and merge
  - **ID:** T-020
  - **Area:** tooling, .claude/skills
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) worker can run `fleet-claim stack "T-A T-B T-C"` and the result is 3 chained PRs (PR-A base=master, PR-B base=PR-A, PR-C base=PR-B); (2) reviewers see and review each PR independently with its own `fleet:approved`/`fleet:needs-fix` label; (3) when PR-A merges, PR-B's base auto-rebases to master without manual intervention (or worker is instructed to rebase as a follow-up); (4) queue-manager correctly handles the cascade: PR-A merge → T-A done, PR-B merge → T-B done, etc.; (5) at least one stack of 3 tasks shipped end-to-end via this flow with no manual intervention
  - **Issue:** #183
  - **Notes:** explicitly [opus] — touches fleet-claim, role files, requires reasoning about edge cases (rebase failures, mid-chain rejections, partial approvals). Current one-PR-multi-commit approach (PR #182) stays in place until this lands. Subcomponents: worker creates chained branches + PRs; queue-manager detects chained PRs and handles cascade; fleet-claim tracks chain state in `~/.fleet/claims/_stack_<agent>/prs`; reviewer notes parent PRs in review. Filed from PR #182; related: jakildev/IrredenEngine#175.
  - **Links:**

- [ ] **Fleet: resumable workflows (molecules) for stacked task chains** — extend `fleet-claim stack` to write a molecule YAML that survives agent crashes, allowing the next worker startup to auto-resume the in-progress step
  - **ID:** T-021
  - **Area:** tooling, .claude/skills
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `fleet-claim stack` creates `~/.fleet/molecules/<name>.yml` with per-task state (pending/in-progress/done/failed); (2) worker crashes mid-stack (kill process during T-002) — on restart, worker reads molecule, resumes or restarts T-002, then completes T-003 without human intervention; (3) `fleet-claim molecule list/resume/advance/complete` commands work correctly; (4) molecule files survive `fleet-up` restarts and are NOT wiped by `fleet-claim clear-all`; (5) worker role files document "check molecule on startup before picking new work" as highest-priority step; (6) at least one real stack crashed-and-resumed in testing
  - **Issue:** #191
  - **Notes:** explicitly [opus] — crash-recovery edge cases (resume vs restart judgment) require real reasoning. Builds on fleet-claim stack from #177. Particularly valuable before lighting batch (T-010…T-016) starts in earnest. Molecule format: yaml with name, agent, created, tasks list (id + state + pr + commit). Inspiration: gas town Molecules concept.
  - **Links:**

- [ ] **Fleet: merger orchestrator pane for auto-resolving PR conflicts** — add a `merger` role that polls for conflicting PRs, auto-resolves mechanical conflicts (TASKS.md sort-merge, whitespace, clean-rebase), and labels non-mechanical conflicts for human
  - **ID:** T-022
  - **Area:** tooling, .claude/skills
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) merger pane in `fleet-up`, polls every 10m; (2) two PRs touching same file far apart get auto-rebased and emerge MERGEABLE; (3) TASKS.md conflicts (two PRs adding different tasks) auto-resolved by sort-merge; (4) non-mechanical conflicts (same code lines changed differently) labeled `human:needs-fix` with conflict description — NEVER auto-resolved; (5) merger never force-pushes to master, never calls `gh pr merge`; (6) every action logged to `~/.fleet/logs/merger.log` and posted as PR comment; (7) PRs labeled `human:wip`, `fleet:wip`, or `fleet:blocker` are skipped
  - **Issue:** #192
  - **Notes:** explicitly [opus] — conflict classification (mechanical vs semantic) requires judgment. v1 scope: TASKS.md sort-merge, whitespace-only, clean-rebase only. More heuristics added incrementally. Uses `--force-with-lease` not `--force`. Inspiration: gas town Refinery role.
  - **Links:**

- [ ] **Fleet: witness health monitoring with heartbeat detection** — add a `witness` role and per-agent heartbeat writes so hung agents (alive but stuck) are detected within 60s and surfaced as alerts
  - **ID:** T-023
  - **Area:** tooling, .claude/skills
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) each agent role file writes `date -u` to `~/.fleet/heartbeats/<agent-name>` every loop iteration (and at major checkpoints for long tasks); (2) `~/.fleet/heartbeats/` populates during normal operation; (3) witness pane in `fleet-up`, polls every 60s; (4) killing an agent with `kill -STOP` triggers a stale-heartbeat log entry within 60s of crossing the per-agent threshold; (5) healthy fleet shows "all healthy (N agents tracked)" with no false positives during normal long-running work; (6) opus-architect (interactive, no loop) is excluded from monitoring; (7) stale alerts written to `~/.fleet/alerts/<agent>.stuck`
  - **Issue:** #193
  - **Notes:** explicitly [sonnet] — pure file-polling and timestamp comparison, no judgment calls. Per-agent thresholds: sonnet-author ≤5m, sonnet-reviewer ≤6m, queue-manager ≤10m, opus-worker ≤30m, opus-reviewer ≤45m. v1 alerts only — no auto-interrupt (too risky). Inspiration: gas town Witness role.
  - **Links:**

- [ ] **Lighting: culling invariants — document off-screen-caster, shadow-ring, light-seed expansion, and AO guard band** — add a canonical "Lighting culling invariants" section to `engine/render/CLAUDE.md` covering the four invariants that downstream lighting PRs (AO, shadows, flood-fill, fog-of-war) must respect to handle off-screen geometry correctly
  - **ID:** T-024
  - **Area:** docs, engine/render
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) new section "Lighting culling invariants" added to `engine/render/CLAUDE.md` containing all four invariants (grid-build iterates full pool not render-culled subset; shadow-ring extent formula when chunk streaming activates; flood-fill seed from all C_LightSource entities within frustum + max(radius) expansion; 1-chunk AO/shadow guard band around view-chunk set); (2) issues #166 (AO), #167 (shadows), #168 (flood-fill), #170 (fog-of-war) cross-linked to issue #196 via comments; (3) T-010 occupancy-grid-build system (already merged in PR #188) checked against invariant #1 — if violated, a follow-up task is filed; if compliant, that is noted in the PR/issue; (4) builds clean on active preset (doc-only change)
  - **Issue:** #196
  - **Notes:** explicitly [sonnet] per issue — this is a write-up and cross-linking task, not a design decision. T-010 (#164) already merged via PR #188; invariant #1 check applies to the merged form. The four invariants were specified by the opus-architect during C_LightSource/C_LightBlocker review (PR #187). Escalate to [opus] only if invariant #1 is found violated in T-010 and structural fixes are needed.
  - **Links:**

---

## In progress

<!-- Tasks currently being worked on. Mirror of [~] items above. -->

- [~] **T-015** — Lighting: LUT palette shading Phase 4 · Owner: render-lut-palette-shading · PR: https://github.com/jakildev/IrredenEngine/pull/198

---

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **Lighting: per-face voxel ambient occlusion (Phase 1)** — compute per-pixel AO values from the 3D occupancy grid for all visible voxel faces and write to an AO texture consumed by the lighting application pass
  - **ID:** T-012
  - **Area:** engine/render, shaders/glsl
  - **Model:** opus
  - **Owner:** render-ao-phase1
  - **Blocked by:** (none)
  - **Acceptance:** (1) AO values computed from occupancy grid for all visible voxel faces; (2) darkened creases visible between adjacent voxels at voxel junctions; (3) shapes in occupancy grid also receive AO; (4) render debug screenshot: with/without AO shows visible darkening; (5) performance < 0.5ms added per frame at typical voxel counts; (6) builds clean on active preset
  - **Issue:** #166
  - **Notes:** AO formula: `if (side1 && side2) ao = 0; else ao = 3 - (side1 + side2 + corner)`. Fixed iso camera means only 3 faces visible per voxel — halves work vs. free-camera. Prefer Option B (separate AO compute pass writing to AO texture) over inline sampling in Stage 2 — cleaner separation and enables shape AO too. Per-sub-pixel approach: 2 sub-pixels per trixel face (2×3 workgroup), sample 3 neighbors directly in compute shader.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/197

- [x] **Lighting: 3D occupancy grid infrastructure** — build the foundational camera-independent 3D data structure representing voxel occupancy that all lighting phases (AO, shadows, flood-fill, fog-of-war LOS) depend on
  - **ID:** T-010
  - **Area:** engine/render, shaders/glsl
  - **Model:** opus
  - **Owner:** render-occupancy-grid
  - **Blocked by:** (none)
  - **Acceptance:** (1) 3D occupancy bitfield populated from voxel pool data; (2) per-axis columnar span lists buildable from the grid; (3) occupancy data accessible from GPU compute shaders; (4) dirty-chunk tracking avoids full rebuild each frame; (5) builds clean on active preset; (6) performance measured and reported
  - **Issue:** #164
  - **Notes:** foundational data structure for all lighting — unblocked T-012, T-013, T-014, T-016 on merge.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/188

- [x] **Skill: attach-screenshots foundation — engine demo support** — create `.claude/skills/attach-screenshots/SKILL.md` with capture flow for engine rendering PRs, committing before/after screenshots to `docs/pr-screenshots/<branch>/`
  - **ID:** T-018
  - **Area:** tooling, .claude/skills
  - **Model:** opus
  - **Owner:** skill-attach-screenshots
  - **Blocked by:** (none)
  - **Acceptance:** (1) `.claude/skills/attach-screenshots/SKILL.md` exists with documented API; (2) skill invokes `render-debug-loop` to capture before/after screenshots; (3) screenshots committed to `docs/pr-screenshots/<branch>/`; (4) skill returns markdown snippet with inline GitHub raw URLs; (5) fallback to `IRShapeDebug`; (6) failure modes reported cleanly; (7) tested end-to-end; (8) builds clean on active preset
  - **Issue:** #186
  - **Notes:** foundation task — T-019 and game T-003 were blocked on this; both unblocked now.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/194

- [x] **Lighting: C_LightSource component and light type enum** — define LightType enum, C_LightSource and C_LightBlocker components in engine prefabs, with Lua bindings for creating and querying light source entities
  - **ID:** T-017
  - **Area:** engine/prefabs/irreden/render
  - **Model:** sonnet
  - **Owner:** prefabs-light-source-component
  - **Blocked by:** (none)
  - **Acceptance:** (1) LightType enum (DIRECTIONAL, POINT, EMISSIVE, SPOT) in engine/prefabs/irreden/render/components/; (2) C_LightSource with type_, emitColor_, intensity_, radius_, direction_[3] defined; (3) C_LightBlocker with blocksLOS_, castsShadow_, opacity_ defined; (4) Lua bindings expose light source creation and configuration; (5) components registered in ECS and queryable by lighting systems; (6) builds clean on active preset
  - **Issue:** #171
  - **Notes:** explicitly [sonnet] in issue — component and enum definitions plus Lua binding boilerplate. No dependencies; lighting phases consume these components when ready.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/187

- [x] **Lighting: screen-space lighting application pass** — insert a new `LIGHTING_TO_TRIXEL` pipeline stage that reads world-space lighting data (AO, shadows, flood-fill) and modulates the trixel canvas in screen-space, applying uniformly to voxels and shapes
  - **ID:** T-011
  - **Area:** engine/render, shaders/glsl
  - **Model:** opus
  - **Owner:** render-lighting-to-trixel-stage
  - **Blocked by:** (none)
  - **Acceptance:** (1) new pipeline stage `LIGHTING_TO_TRIXEL` inserted between last geometry stage and TRIXEL_TO_TRIXEL; (2) with no lighting data bound, pass is a no-op — existing rendering unchanged; (3) when lighting data is available, all voxel and shape canvas pixels are modulated; (4) GUI/UI elements are NOT modulated; (5) verified via render debug screenshot with/without pass; (6) builds clean on active preset
  - **Issue:** #165
  - **Notes:** ships as no-op skeleton; later phases activate it by binding AO/shadow/flood-fill data.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/185

- [x] **Render: add `SubdivisionMode` enum (NONE / POSITION_ONLY / FULL)** — replace the two-value `VoxelRenderMode` with a three-value `SubdivisionMode` that decouples smooth positioning from shape-fidelity scaling
  - **ID:** T-009
  - **Area:** engine/render, engine/prefabs/irreden/render, engine/world
  - **Model:** opus
  - **Owner:** render-subdivision-mode
  - **Blocked by:** (none)
  - **Acceptance:** (1) `SubdivisionMode` enum with NONE/POSITION_ONLY/FULL replaces `VoxelRenderMode`; (2) Lua key `"subdivision_mode"` accepts `"none"` / `"position"` / `"full"`, default `"full"`; (3) all three modes build clean on active preset; (4) visual: NONE snaps to grid, POSITION_ONLY at zoom 2 shows identical silhouette to 2×-radius-at-zoom-1, FULL at zoom 2 is visibly smoother; (5) FULL mode (current SMOOTH behavior) has not regressed
  - **Issue:** #156
  - **Notes:** `POSITION_ONLY` is the new mode — subdivisions apply to positioning only; SDF evaluates at base-zoom coarse grid. Key files: `VoxelRenderMode` enum definition, RenderManager getters/setters, `system_shapes_to_trixel.hpp` ~line 284 (third branch for POSITION_ONLY), WorldConfig Lua wiring. Per-entity subdivision modes are future work — note in code but do not implement.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/160

- [x] **macOS/Metal build maturation: get `macos-debug` preset green end-to-end** —
  mirror of the Linux-maturation task, on the Mac side. Umbrella epic
  for fixing every compile/link/runtime issue in the Metal backend
  as the fleet surfaces them.
  - **ID:** T-003
  - **Area:** engine/render/src/metal, engine/render/src/shaders/metal,
    anywhere the Metal path breaks
  - **Model:** opus (backend/render work is Opus territory)
  - **Owner:** metal-build-maturation
  - **Blocked by:** (none)
  - **Acceptance:** from a fresh macOS clone at `~/src/IrredenEngine`,
    `cmake --preset macos-debug && cmake --build build -j$(sysctl -n hw.ncpu)`
    builds `IRShapeDebug`, `IRCreationDefault`, and `IrredenEngineTest`
    with zero warnings escalated to errors, and `IRShapeDebug` launches
    and renders the same reference frame as the OpenGL backend.
  - **Notes:** see `docs/AGENT_FLEET_SETUP.md` §10 (macOS subsection)
    for the known first-time issues on macOS — Objective-C++ flags,
    shader parity gaps, FFmpeg `pkg-config` path, Metal 3 target
    version, Retina scaling.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/154

- [x] **macOS FFmpeg: fix CMake/pkg-config wiring on `macos-debug`** — get FFmpeg headers and libs found and linked correctly on macOS so `engine/video/` compiles and links on the `macos-debug` preset.
  - **ID:** T-002
  - **Area:** build, engine/video
  - **Model:** sonnet
  - **Owner:** build-macos-ffmpeg-pkgconfig-2
  - **Blocked by:** (none)
  - **Acceptance:** `cmake --preset macos-debug && cmake --build build --target IRShapeDebug` completes with zero FFmpeg-related include or linker errors; `avcodec`, `avformat`, `avutil`, and `swscale` all appear in the final link line.
  - **Notes:** Known macOS first-time issue per `docs/AGENT_FLEET_SETUP.md` §10 — Homebrew FFmpeg pkg-config path differs from Linux (`/opt/homebrew/lib/pkgconfig` on Apple Silicon, `/usr/local/lib/pkgconfig` on Intel).
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/128

- [x] **Fix `engine/asset` extension mismatch: `.txl` vs `.irtxl`** —
  `saveTrixelTextureData` writes files with `.txl` but `loadTrixelTextureData`
  opens files expecting `.irtxl`. Standardize both on `.txl`.
  - **Area:** engine/asset
  - **Model:** sonnet
  - **Owner:** asset-extension-fix
  - **Blocked by:** (none)
  - **Acceptance:** both `saveTrixelTextureData` and `loadTrixelTextureData`
    use `.txl`; CMake build passes (`linux-debug` or `macos-debug`);
    `C_TriangleCanvasTextures::saveToFile` followed by `loadFromFile` round-
    trips a canvas without corruption.
  - **Notes:** Bug in `engine/asset/src/ir_asset.cpp` line 32 — change
    `.irtxl` → `.txl`. No `.irtxl` files exist in the repo so there is no
    backward-compat concern.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/96

- [x] **Docs: doc pass on `ir_math_types.hpp` and `color_palettes.hpp`** —
  add `///` to every public declaration in both files; remove dead commented-
  out code (the `kFaceCameraRotations` stub in `ir_math_types.hpp` and the
  commented-out alternative color values in `color_palettes.hpp`).
  - **Area:** engine/math
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-2
  - **Blocked by:** (none)
  - **Acceptance:** every public enum, struct, field, and constant in both
    files has a `///` doc comment; no `//`-commented-out dead code remains;
    all existing `///` comments from prior passes are preserved.
  - **Notes:** skip `kInvisable` typo rename — that affects a prefab header
    in a different module and belongs in its own PR.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/90

- [x] **Unit tests for iso-projection and math helpers in ir_math.hpp** —
  add `test/math/ir_math_test.cpp` covering the untested `constexpr`/`inline`
  helpers that `physics_test.cpp` does not exercise.
  - **Area:** engine/math
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-2
  - **Blocked by:** (none)
  - **Acceptance:** `test/math/ir_math_test.cpp` exists and is listed in
    `test/CMakeLists.txt`; `IrredenEngineTest` builds and all new tests pass;
    coverage includes `pos3DtoPos2DIso` (ivec3 and vec3 overloads),
    `pos3DtoDistance`, `isoDepthShift`, `lerpByte`, `lerpColor`, `lerpHSV`,
    `IsoBounds2D::contains`/`center`/`extent`/`fromCorners`, `entityIsoBounds`,
    `divCeil`, `index2DtoIndex1D`, `index3DtoIndex1D`.
  - **Notes:** skip `pos3DtoPos2DScreen` and `screenDeltaToIsoDelta` — they
    depend on `IRPlatform::kGfx` compile-time constants and are platform-specific.
    If a test uncovers a math bug, requeue the fix as `[opus]`.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/79

- [x] **Example: unit tests for engine/math/physics.hpp** — exhaustive
  tests for ballistic helpers.
  - **Area:** engine/math
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-1
  - **Blocked by:** (none — unit tests don't care which platform builds them)
  - **Acceptance:** new test binary builds, tests cover all four physics
    helpers (`impulseForHeight`, `flightTimeForHeight`, `heightForImpulse`,
    `isTunnelingSafe`) with edge cases, all pass.
  - **Notes:** pattern-heavy; the function spec is in
    `engine/math/CLAUDE.md` under "Physics". If a test uncovers a real bug
    in the helpers, stop and requeue as `[opus]` with a bug report rather
    than fixing inline.
  - **Links:** https://github.com/jakildev/IrredenEngine/pull/79
