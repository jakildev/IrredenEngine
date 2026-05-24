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


- [~] **System: pre-resolve component-vector refs on main thread before PARALLEL_FOR dispatch** — eliminate operator[] race on EntityManager from worker threads; pre-bind component vectors before parallelFor call
  - **ID:** T-333
  - **Area:** engine/system, engine/entity
  - **Model:** opus
  - **Owner:** claude/T-333-pre-resolve-component-vectors
  - **Blocked by:** (none)
  - **Acceptance:** worker bodies under PARALLEL_FOR no longer call m_pureComponentTypes::operator[]; ThreadSanitizer-clean parallelFor dispatch over archetype with >=kDefaultGrainSize entities; VELOCITY_3D continues to tick correctly
  - **Issue:** #1105
  - **Notes:** Deferred from T-222 review (PR #1097, Opus recheck nit #1). Latent UB: getComponentData<C>(node) calls m_pureComponentTypes[typeName] (operator[], non-const) from each worker. Works today on libstdc++/libc++/MSVC by coincidence; real race as PARALLEL_FOR broadens (T-223, T-225). Cleanest fix: pre-resolve component vectors in SystemManager::executeSystem before IRJobs::parallelFor, pass as std::tuple into rangedFn. Smaller fix: switch to m_pureComponentTypes.at(typeName). Part of epic #226. Should land before T-223.
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

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

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
- [x] **T-326** — demos: adopt standardControlSystems() bundle across all demos · Owner: claude/T-326-adopt-standard-camera-bundle · PR: https://github.com/jakildev/IrredenEngine/pull/1095
