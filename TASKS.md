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

- [~] **Fleet: roles read scout cache instead of running gh/git directly** — update all six worker/reviewer/queue-manager/merger role files to read `~/.fleet/state/state.json` instead of running per-iteration gh/git list calls
  - **ID:** T-039
  - **Area:** .claude/commands
  - **Model:** opus
  - **Owner:** claude/T-039-roles-read-scout-cache
  - **Blocked by:** (none)
  - **Acceptance:** (1) each affected role's startup reads `~/.fleet/state/state.json` instead of inline gh/git list calls; (2) per-item ops (gh pr view, gh pr diff) remain inline; (3) architect role files unchanged; (4) smoke test verifies same work items found via cache as via direct queries; (5) no regression in any role's dispatch behavior
  - **Issue:** #271
  - **Notes:** affects six files: role-sonnet-author.md, role-opus-worker.md, role-sonnet-reviewer.md, role-opus-reviewer.md, role-queue-manager.md, role-merger.md. Architects untouched. Mostly mechanical rewrites but spans whole worker fleet — careful to preserve each role's filter logic. Difficulty: medium.
  - **Links:**

- [ ] **Fleet: trigger-aware back-off in fleet-babysit** — extend fleet-babysit to sleep a long back-off (~30 min) when no trigger file exists, relaunch immediately when a trigger appears
  - **ID:** T-040
  - **Area:** scripts/fleet
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-039
  - **Acceptance:** (1) worker babysit sleeps LONG_BACKOFF_SECONDS (~1800) when no trigger file exists; (2) fires immediately (within ~5s) when trigger appears mid-sleep; (3) trigger file removed after consumption; (4) architect babysit lifecycle unchanged; (5) shutdown sentinel from PR #269 still wins; (6) `bash -n` clean; (7) real fleet test: no worker iterates more than once per LONG_BACKOFF_SECONDS on a quiet queue
  - **Issue:** #272
  - **Notes:** hybrid approach — keeps babysit hardening from PRs #246/#247/#248/#257/#265/#269, adds trigger-awareness for ~80% cost-saving benefit without full transient dispatcher replacement. New env var FLEET_LONG_BACKOFF (default 1800). Difficulty: medium.
  - **Links:**

- [~] **Linux build maturation: get `linux-debug` preset green end-to-end** —
  fix every compile/link/runtime issue encountered when building the
  engine against the new `linux-debug` CMake preset inside WSL2 Ubuntu
  24.04. This is the umbrella epic — break it into smaller follow-up
  tasks as concrete issues are found.
  - **ID:** T-001
  - **Area:** engine/* (anywhere the Linux path breaks)
  - **Model:** opus (for anything touching core-engine invariants) /
    sonnet (for mechanical port fixes like missing `#include`, case-
    sensitive paths, EOL drift)
  - **Owner:** T-001-linux-ci
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

- [~] **Fleet: stacked-PR: start-next-task stack-aware reset** — branch off just-opened PR's head ref instead of `origin/master` when active molecule has remaining tasks
  - **ID:** T-042
  - **Area:** .claude/skills
  - **Model:** opus
  - **Owner:** claude/T-042-start-next-task-stack-aware
  - **Blocked by:** (none)
  - **Stack:** T-041..T-045 stacked-pr-vision
  - **Acceptance:** after opening PR for first task in a 2-task stack, `start-next-task` lands on a branch whose merge-base is the just-opened PR's head ref; after exhausting the stack, resets to `origin/master`
  - **Issue:** #289
  - **Notes:** Part 2 of 5. See `.fleet/plans/T-042.md`.
  - **Links:**

- [ ] **Fleet: stacked-PR: reviewer upstream approval gating** — hold downstream PR approval when upstream has `fleet:needs-fix`; gate on upstream `fleet:approved` before reviewing own diff
  - **ID:** T-043
  - **Area:** scripts/fleet, .claude/commands
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-042
  - **Stack:** T-041..T-045 stacked-pr-vision
  - **Acceptance:** downstream PR open while upstream has `fleet:needs-fix` → `fleet:awaiting-upstream-review` applied, no `fleet:approved` issued; after upstream re-approved, next reviewer pass proceeds normally
  - **Issue:** #289
  - **Notes:** Part 3 of 5. New label: `fleet:awaiting-upstream-review`. See `.fleet/plans/T-043.md`.
  - **Links:**

- [ ] **Fleet: stacked-PR: downstream auto-rebase when upstream changes** — add `fleet-claim molecule rebase-downstream` subcommand; invoke in author role after addressing upstream review feedback
  - **ID:** T-044
  - **Area:** scripts/fleet, .claude/commands
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-043
  - **Stack:** T-041..T-045 stacked-pr-vision
  - **Acceptance:** stack A→B→C; reviewer flags A with `fleet:needs-fix`; worker fixes A, pushes, runs rebase subcommand; B and C branches now have new A tip as parent; PRs get comment; conflicts surface as `fleet:blocker` + comment, chain pauses
  - **Issue:** #289
  - **Notes:** Part 4 of 5. New subcommand: `fleet-claim molecule rebase-downstream`. Use `--force-with-lease`, never `--force`. See `.fleet/plans/T-044.md`.
  - **Links:**

- [ ] **Fleet: stacked-PR: TASKS.md Stack: field for chain visibility** — add `Stack:` field to task template; queue-manager populates when ingesting child issues from a shared parent epic
  - **ID:** T-045
  - **Area:** TASKS.md template, .claude/commands
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-044
  - **Stack:** T-041..T-045 stacked-pr-vision
  - **Acceptance:** child task shows `Stack:` populated; standalone tasks omit the field
  - **Issue:** #289
  - **Notes:** Part 5 of 5. Touches TASKS.md template and `role-queue-manager.md`. See `.fleet/plans/T-045.md`.
  - **Links:**

---

## In progress

<!-- Tasks currently being worked on. Mirror of [~] items above. -->


---

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-041** — Fleet: stacked-PR: commit-and-push stack-aware mode · Owner: claude/T-041-stacked-pr-skill · PR: https://github.com/jakildev/IrredenEngine/pull/292
- [x] **T-038** — Fleet: add fleet-state-scout daemon for shared state caching · Owner: claude/T-038-fleet-state-scout · PR: https://github.com/jakildev/IrredenEngine/pull/291
- [x] **T-037** — Fleet/merger: stacked-PR awareness via baseRefName · Owner: T-037-merger-stacked-awareness · PR: https://github.com/jakildev/IrredenEngine/pull/290
- [x] **T-035** — Prefab refactor: relocate debug overlay API from IRRender:: to prefab namespace · Owner: T-035-debug-overlay-prefab · PR: https://github.com/jakildev/IrredenEngine/pull/276
- [x] **T-034** — Prefab refactor: relocate fog-of-war API from IRRender:: to prefab namespace · Owner: T-034-fog-prefab-namespace · PR: https://github.com/jakildev/IrredenEngine/pull/275
- [x] **T-036** — Prefab refactor: relocate sun lighting API from IRRender:: to prefab namespace · Owner: T-036-sun-prefab-namespace · PR: https://github.com/jakildev/IrredenEngine/pull/278
- [x] **T-032** — Remove engine-side midi_polyrhythm demo after game port lands · Owner: T-032-remove-midi-polyrhythm · PR: https://github.com/jakildev/IrredenEngine/pull/274
- [x] **T-033** — engine/render CLAUDE.md: install layering principle between render and prefabs · Owner: T-033-render-prefab-layering-doc · PR: https://github.com/jakildev/IrredenEngine/pull/267
- [x] **T-029** — Fleet: cross-host smoke-test running-tally for render changes · Owner: T-029-cross-host-smoke-tally · PR: https://github.com/jakildev/IrredenEngine/pull/262
- [x] **T-007** — Wire up a `backend-parity` dry run · Owner: metal-finish-parity · PR: https://github.com/jakildev/IrredenEngine/pull/260
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
