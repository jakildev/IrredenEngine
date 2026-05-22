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


- [~] **voxel: GRID-mode rotation re-rasterizes voxels on transform change (C6)** — SYSTEM_REBUILD_GRID_VOXELS runs on entities with changed C_LocalTransform; rotates authored voxels to world-grid cells; last-writer-wins on cell collisions (deterministic by entity ID)
  - **ID:** T-294
  - **Area:** engine/prefabs/irreden/voxel, engine/render
  - **Model:** opus
  - **Owner:** claude/T-294-grid-mode-rotation
  - **Blocked by:** (none)
  - **Acceptance:** (1) A cube rotated 45° around Z occupies a different set of world voxel cells than at 0°; (2) rotation snaps to grid (aliasing accepted by design — documented in the system header); (3) deterministic across frames; cell collisions documented; (4) interacts cleanly with Epic E E5 (entity chunk migration) for rotated entities crossing chunk boundaries; (5) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #957
  - **Notes:** Off-stack fork from C3; does NOT block S-C-core's C3 → C7 chain. Branch from C3 PR head. Push-at-mutation; no dirty flag per cpp-ecs. Plan ref: `.claude/plans/okay-lets-go-through-idempotent-giraffe.md` §"Epic C → C6".
  - **Links:**

- [ ] **engine: retire C_Position3D / C_PositionGlobal3D / C_Rotation legacy components** — delete all four legacy position/rotation component headers; remove from createEntity auto-attach set; migrate Lua bindings and script API to SQT equivalents; update CLAUDE.md
  - **ID:** T-302
  - **Area:** engine/prefabs/irreden/common, engine/script
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Stack:** T-299..T-302 sqt-phase-a
  - **Acceptance:** (1) grep -r "C_PositionGlobal3D" engine/ returns zero hits; (2) grep -r "C_Position3D" engine/ returns zero hits; (3) grep -r "C_Rotation" engine/ returns zero hits; (4) createEntity no longer auto-attaches legacy components; (5) fleet-build clean on linux-debug and macos-debug; (6) full test suite passes (test/ecs/*, test/asset/*, render-verify reference set); (7) IRShapeDebug, voxel editor, spring/note demos render identically; (8) CLAUDE.md retirement note added
  - **Issue:** #987
  - **Notes:** T-199d — deletion phase, closes the #736 migration chain. Deletions: component_position_3d.hpp + _lua.hpp, component_position_global_3d.hpp, component_position_offset_3d.hpp (if not already deleted by epic #731 modifier migration), component_rotation.hpp. Watch for creations/ consumers missed by engine audit — build fails loudly. Lua migrations: engine/script/src/prefab_api.cpp and lua_sprite_namespace.hpp. This closes a multi-phase migration — diff should be mostly red. Stacks on T-301.
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-295** — DETACHED canvas SO(3) rotation · Owner: claude/t295-canvas-so3 · PR: https://github.com/jakildev/IrredenEngine/pull/1047
- [x] **T-313** — perf: Lua-vs-C++ parity dashboard · Owner: claude/T-313-lua-cpp-parity-dashboard · PR: https://github.com/jakildev/IrredenEngine/pull/1037
- [x] **T-311** — perf: CI baseline + automated regression gate for engine/render, engine/system, engine/math PRs · Owner: claude/T-311-ci-baseline-gate · PR: https://github.com/jakildev/IrredenEngine/pull/1039
- [x] **T-305** — math: IRMath::SDF::evaluateGrid batch helper + refactor applyFillSDF · Owner: claude/T-305-sdf-grid-batch · PR: https://github.com/jakildev/IrredenEngine/pull/1029
- [x] **T-307** — skills: decompose /simplify into parallel reuse-detection subagents · Owner: claude/T-307-simplify-subagent-decomposition · PR: https://github.com/jakildev/IrredenEngine/pull/1040
- [x] **T-309** — render: split visible vs shadow-feeder voxel compaction (design doc) · Owner: claude/T-309-feeder-split · PR: https://github.com/jakildev/IrredenEngine/pull/1036
- [x] **T-310** — render: graceful per-pair fallback for Metal timestamp allocation · Owner: claude/T-310-async-gpu-timers · PR: https://github.com/jakildev/IrredenEngine/pull/1035
- [x] **T-312** — perf: Catch2 microbench harness for engine/math hot paths · Owner: claude/T-312-math-microbench-harness · PR: https://github.com/jakildev/IrredenEngine/pull/1034
- [x] **T-308** — demos: named config preset files (IRPerfGrid + friends) · Owner: claude/T-308-config-preset-flag · PR: https://github.com/jakildev/IrredenEngine/pull/1032
- [x] **T-304** — render: extract mask-grid pixel packing into renderer helper · Owner: claude/T-304-render-mask-grid-helper · PR: https://github.com/jakildev/IrredenEngine/pull/1031
- [x] **T-306** — asset: scene_io metadata index + voxel-record byte constant dedup · Owner: claude/T-306-scene-io-metadata-index · PR: https://github.com/jakildev/IrredenEngine/pull/1030
- [x] **T-303** — math: IRMath grid-iteration and 3D-mask helpers · Owner: claude/T-303-irmath-grid-helpers · PR: https://github.com/jakildev/IrredenEngine/pull/1028
- [x] **T-291** — wire detached canvas rotation through composite TRS (C3) · Owner: claude/T-291-detached-canvas-rotation · PR: https://github.com/jakildev/IrredenEngine/pull/1003
- [x] **T-293** — render geometric trixel deformation (replaces T-322 bilinear residual) · Owner: claude/T-293-geometric-trixel-deformation · PR: https://github.com/jakildev/IrredenEngine/pull/1005
- [x] **T-292** — math: continuous-yaw + deformation math helpers (C5) · Owner: claude/T-292-yaw-deformation-math · PR: https://github.com/jakildev/IrredenEngine/pull/1002
- [x] **T-301** — migrate C_VoxelPool and ~10 voxel-pipeline files off legacy position components; architect call required on pool SoA layout (Option A: C_WorldTransform array vs Option B: position-only projection arrays) · Owner: (auto-reaped) · PR: https://github.com/jakildev/IrredenEngine/issues/986
- [x] **T-290** — C_RotationMode enum + component (C2) · Owner: claude/T-290-rotation-mode-component · PR: https://github.com/jakildev/IrredenEngine/pull/1001
- [x] **T-289** — voxel: push-at-mutation position upload (no per-frame re-upload) (B5) · Owner: claude/T-289-voxel-pos-push-at-mutation · PR: https://github.com/jakildev/IrredenEngine/pull/999
- [x] **T-298** — world: chunk disk persistence + lazy load (E6) · Owner: claude/T-298-chunk-disk-persistence · PR: https://github.com/jakildev/IrredenEngine/pull/998
- [x] **T-299** — migrate ~11 render-pipeline systems from C_PositionGlobal3D/C_Rotation to C_WorldTransform (pure reader migration, no writer changes in this phase) · Owner: (auto-reaped) · PR: https://github.com/jakildev/IrredenEngine/issues/984
