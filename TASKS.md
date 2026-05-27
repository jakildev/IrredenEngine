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

- [ ] **fleet: delete TASKS.md, switch T-NNN convention to issue numbers, update all docs** — delete TASKS.md and game TASKS.md; update fleet-claim to accept issue numbers (reject T-NNN with hint); update commit/branch/plan-file naming conventions; update ~7 role docs, ~10 skills, CLAUDE.md, FLEET.md, and test fixtures
  - **ID:** T-382
  - **Area:** tooling, docs
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Stack:** T-380..T-382 tasks-to-issues
  - **Acceptance:** (1) TASKS.md no longer exists; (2) all role docs and skills reference issue numbers and `fleet-queue-list`; (3) `fleet-claim` accepts issue numbers, rejects T-NNN with helpful message; (4) commit messages and branches use issue numbers; (5) test suite passes with updated fixtures
  - **Issue:** #1216
  - **Notes:** Part of plan `.claude/plans/can-we-do-a-delightful-sutherland.md` (Phases 5–6). PR 3 of 3. Completes TASKS.md elimination — queue-related commits drop to near zero.
  - **Links:**

- [~] **render: sun shadow AABB sweep uses mismatched coordinate frames at non-zero yaw** — rotate iso frustum corners from raster→world frame via `rotateCardinalZInv` before sun-space sweep in `system_bake_sun_shadow_map.hpp`
  - **ID:** T-387
  - **Area:** engine/render
  - **Model:** sonnet
  - **Owner:** claude/T-387-shadow-aabb-coordinate-frame
  - **Blocked by:** (none)
  - **Acceptance:** At non-zero yaw, shadow AABB covers correct world-space region; no shadow clip at viewport edges; OR confirm #1198 (cascaded shadow maps, merged 2026-05-27) already resolves this and close the issue
  - **Issue:** #1220
  - **Notes:** Affected file: `system_bake_sun_shadow_map.hpp:199-205`. Fix is 3-4 lines (see issue body). **May be superseded**: PR #1198 (cascaded shadow maps) merged 2026-05-27 computes per-cascade AABBs in world frame. Worker should first verify on current master — if the cascaded system resolves the mismatch, close #1220 instead of applying the fix.
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

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-381** — fleet: scout reads issues instead of TASKS.md, eliminate queue-tick maintenance sync · Owner: claude/T-381-scout-reads-issues · PR: https://github.com/jakildev/IrredenEngine/pull/1229
- [x] **T-386** — render: chunk visibility bounds rotation-aware · Owner: claude/T-386-chunk-visibility-rotation · PR: https://github.com/jakildev/IrredenEngine/pull/1228
- [x] **T-385** — render: face normal not rotated in lighting/shadow shaders at non-zero camera yaw · Owner: claude/T-385-face-normal-rotation-lighting-shadow · PR: https://github.com/jakildev/IrredenEngine/pull/1225
- [x] **T-384** — render: restore device-level distance texture clear (viewport clipping regression) · Owner: claude/T-384-viewport-clipping-regression · PR: https://github.com/jakildev/IrredenEngine/pull/1231
- [x] **T-388** — fleet: semantic-conflict resolution races — add atomic claim label before checkout · Owner: claude/T-388-resolving-label-conflict-claim · PR: https://github.com/jakildev/IrredenEngine/pull/1227
- [x] **T-383** — fleet: commit-and-push — add pre-PR Closes# cross-check · Owner: claude/T-383-commit-push-closes-crosscheck · PR: https://github.com/jakildev/IrredenEngine/pull/1224
- [x] **T-380** — fleet: add model-affinity labels + fleet-queue-list + gate master_lock_task · Owner: claude/T-380-fleet-queue-list-model-labels · PR: https://github.com/jakildev/IrredenEngine/pull/1222
- [x] **T-368** — render: async texture loading API + World icon load POC · Owner: claude/T-368-async-texture-loading · PR: https://github.com/jakildev/IrredenEngine/pull/1205
- [x] **T-372** — world: chunk persistence smoke demo (end-to-end consumer wire-in) · Owner: claude/T-372-chunk-streaming-smoke-demo · PR: https://github.com/jakildev/IrredenEngine/pull/1208
- [x] **T-373** — world: rename ChunkDiskPersistence → ChunkVoxelDiskPersistence · Owner: claude/T-373-rename-chunk-disk-persistence · PR: https://github.com/jakildev/IrredenEngine/pull/1207
- [x] **T-379** — system: bulk PARALLEL_FOR migration of trivially-safe prefab systems · Owner: claude/T-379-parallel-for-bulk-migration · PR: https://github.com/jakildev/IrredenEngine/pull/1212
- [x] **T-378** — system: PROPAGATE_TRANSFORM BFS-parallel refactor · Owner: claude/T-378-propagate-transform-bfs-parallel · PR: https://github.com/jakildev/IrredenEngine/pull/1203
- [x] **T-374** — fleet: scope-shipped detection pass in queue-manager ingest · Owner: claude/T-374-queue-manager-scope-shipped-check · PR: https://github.com/jakildev/IrredenEngine/pull/1210
- [x] **T-377** — fleet: commit-and-push — refuse to commit when staged tree is empty · Owner: claude/T-377-commit-and-push-empty-commit-guard · PR: https://github.com/jakildev/IrredenEngine/pull/1209
- [x] **T-375** — fleet-tasks-render — preserve [~] from cross-host fleet:claim-* labels · Owner: claude/T-375-cross-host-gh-claim-preserve · PR: https://github.com/jakildev/IrredenEngine/pull/1206
- [x] **T-376** — fleet-claim cleanup --gh — TTL sweep stale fleet:claim-* labels off open issues · Owner: claude/T-376-fleet-claim-ttl-sweep · PR: https://github.com/jakildev/IrredenEngine/pull/1204
- [x] **T-370** — perf: cap UPDATE ticks per frame to prevent IRPerfGrid death spiral · Owner: claude/T-370-perfgrid-update-pipeline · PR: https://github.com/jakildev/IrredenEngine/pull/1202
- [x] **T-369** — add IRMath::cbrt and migrate perf_grid off std::cbrt · Owner: claude/T-369-irmath-cbrt · PR: https://github.com/jakildev/IrredenEngine/pull/1201
- [x] **T-371** — world: chunk persistence — two-level directory split · Owner: claude/T-371-chunk-persistence-two-level-dir-split · PR: https://github.com/jakildev/IrredenEngine/pull/1200
- [x] **T-367** — tooling: /increase-complexity skill — auto-grow demos with new engine systems and entity count · Owner: claude/T-367-increase-complexity-skill · PR: https://github.com/jakildev/IrredenEngine/pull/1199
