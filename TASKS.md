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


- [~] **asset: async texture loading via pinned worker (T-226 Phase 5)** — `IRAsset::loadTextureAsync` returns immediately with an AssetHandle; disk read + decode runs on a pinned I/O worker; GL texture upload schedules onto main thread
  - **ID:** T-368
  - **Area:** engine/system
  - **Model:** opus
  - **Owner:** claude/T-368-async-texture-loading
  - **Blocked by:** (none)
  - **Acceptance:** (1) `IRAsset::loadTextureAsync` exists, returns `AssetHandle<C_Texture>`, produces a valid texture once resolved; (2) one existing blocking startup load migrated to async; (3) startup-time delta filed in `docs/perf-reports/`; (4) no regression on `perf_grid_matrix.sh`
  - **Issue:** #1073
  - **Notes:** Phase 5 of multithreading epic (#226). Blocker #1068 (IRJobs::pinTo) is now closed. GL/Metal texture creation must happen on the main thread — pinned worker does disk-read + decode only; upload schedules onto main. One entry point POC only; other loaders follow if the pattern is right.
  - **Links:**

- [~] **world: chunk persistence — wire in-engine consumer end-to-end (T-298 follow-up 3/4)** — pick one consumer (voxel editor save path or new IRChunkStreamingSmoke demo) and wire a real `ChunkResidencyManager` + `VoxelPoolAllocation` round-trip
  - **ID:** T-372
  - **Area:** engine/world
  - **Model:** sonnet
  - **Owner:** claude/T-372-chunk-streaming-smoke-demo
  - **Blocked by:** (none)
  - **Acceptance:** (1) Consumer wired and running on linux-debug and macos-debug; (2) chunk file lands under `<save>/chunks/`; (3) round-trip preserves voxel data; (4) clean chunks skip the save
  - **Issue:** #1170
  - **Notes:** Split from #1008 item 3. T-298's code path has no in-engine consumer — only gtest fake-pool integration. Candidates: voxel editor "save chunks" path, or new `IRChunkStreamingSmoke` demo. Should land before E2/E3 (T-357/T-358) so persistence is proven on a real pool.
  - **Links:**

- [~] **world: rename ChunkDiskPersistence → ChunkVoxelDiskPersistence (T-298 follow-up 4/4)** — rename to make explicit the class saves the voxel-pool slice only, not entity manifest or billboard metadata
  - **ID:** T-373
  - **Area:** engine/world
  - **Model:** sonnet
  - **Owner:** claude/T-373-rename-chunk-disk-persistence
  - **Blocked by:** (none)
  - **Acceptance:** All `ChunkDiskPersistence` references in engine/ replaced with `ChunkVoxelDiskPersistence`; fleet-build clean on linux-debug and macos-debug
  - **Issue:** #1171
  - **Notes:** Split from #1008 item 4. Low-priority rename pass. May fold into T-371 or T-372 if convenient; standalone skip is fine. Prevents future PRs from assuming entity state is durable via this class.
  - **Links:**

- [~] **system: PROPAGATE_TRANSFORM BFS-parallel refactor (T-332 follow-up)** — refactor PROPAGATE_TRANSFORM into a two-pass BFS-parallel design; serial pre-sort builds a per-depth level index, then parallelFor dispatches all entities at each depth independently
  - **ID:** T-378
  - **Area:** engine/system, engine/entity
  - **Model:** opus
  - **Owner:** claude/T-378-propagate-transform-bfs-parallel
  - **Blocked by:** (none)
  - **Acceptance:** (1) `PROPAGATE_TRANSFORM` dispatches via `PARALLEL_FOR` at per-depth-level granularity; (2) `perf_grid_matrix.sh` 262K shows ≥2× speedup for that system in isolation; (3) hierarchy-correctness test with ≥5 depth levels produces identical world-transforms as serial baseline; (4) cache-invalidation test: spawn/destroy/reparent triggers re-sort on next tick; (5) `IrredenEngineTest` 100% pass; (6) results filed in `docs/perf-reports/threading_propagate_transform.md`
  - **Issue:** #1195
  - **Notes:** Two-pass approach: serial `beginTick` builds a BFS level-indexed structure (entities at depth N can be composed in parallel once depth N-1 is finalized); then `parallelFor` per depth level with implicit barrier between levels. Cache the level structure; invalidate only on structural hierarchy changes (spawn/destroy/reparent). Flat hierarchies (depth 1) become a single wide `parallelFor`. Dominant UPDATE cost at 262K entities (~2.51ms/frame, ~40% of UPDATE time). Referenced in threading_phase3.md as T-332; unblocks 100fps target (#1052). Part of epic #226.
  - **Links:**

- [~] **system: bulk PARALLEL_FOR migration of trivially-safe prefab systems** — inventory all prefab systems, classify by access-derivation safety rules, annotate ≥10 safe systems with PARALLEL_FOR, verify via validator, and file perf measurements
  - **ID:** T-379
  - **Area:** engine/prefabs, engine/system
  - **Model:** sonnet
  - **Owner:** claude/T-379-parallel-for-bulk-migration
  - **Blocked by:** (none)
  - **Acceptance:** (1) ≥10 additional prefab systems annotated with `PARALLEL_FOR` beyond the 2 already done; (2) `IrredenEngineTest` 100% pass; (3) measurable speedup at 262K entities vs post-Phase-3 baseline; (4) results filed in `docs/perf-reports/threading_bulk_migration.md`; (5) `engine/system/CLAUDE.md` updated with full list of parallelized systems; (6) no regression in `IRShapeDebug --auto-screenshot 60`
  - **Issue:** #1196
  - **Notes:** Mechanical sweep: grep for `registerSystem<`/`createSystem<` across `engine/prefabs/`, build inventory table, classify each system. Safe criteria: per-component tick refs only, no `EntityId` first param, no manager/GL/Metal/sol2/LuaScript calls in body, no static or shared-state access. Likely candidates: ANIMATION_COLOR, ANIMATION_FRAMES, PERIODIC_IDLE, PERIODIC_IDLE_POSITION_OFFSET, LIFETIME, MODIFIER_DECAY, VELOCITY_ROTATION, SCALE_ANIMATION, FADE_IN, FADE_OUT. Check ANIMATION_COLOR — may already be annotated from T-222 POC. Do not reorganize pipeline groups in this PR. Part of epic #226.
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-374** — fleet: scope-shipped detection pass in queue-manager ingest · Owner: claude/T-374-queue-manager-scope-shipped-check · PR: https://github.com/jakildev/IrredenEngine/pull/1210
- [x] **T-377** — fleet: commit-and-push — refuse to commit when staged tree is empty · Owner: claude/T-377-commit-and-push-empty-commit-guard · PR: https://github.com/jakildev/IrredenEngine/pull/1209
- [x] **T-375** — fleet-tasks-render — preserve [~] from cross-host fleet:claim-* labels · Owner: claude/T-375-cross-host-gh-claim-preserve · PR: https://github.com/jakildev/IrredenEngine/pull/1206
- [x] **T-376** — fleet-claim cleanup --gh — TTL sweep stale fleet:claim-* labels off open issues · Owner: claude/T-376-fleet-claim-ttl-sweep · PR: https://github.com/jakildev/IrredenEngine/pull/1204
- [x] **T-370** — perf: cap UPDATE ticks per frame to prevent IRPerfGrid death spiral · Owner: claude/T-370-perfgrid-update-pipeline · PR: https://github.com/jakildev/IrredenEngine/pull/1202
- [x] **T-369** — add IRMath::cbrt and migrate perf_grid off std::cbrt · Owner: claude/T-369-irmath-cbrt · PR: https://github.com/jakildev/IrredenEngine/pull/1201
- [x] **T-371** — world: chunk persistence — two-level directory split · Owner: claude/T-371-chunk-persistence-two-level-dir-split · PR: https://github.com/jakildev/IrredenEngine/pull/1200
- [x] **T-367** — tooling: /increase-complexity skill — auto-grow demos with new engine systems and entity count · Owner: claude/T-367-increase-complexity-skill · PR: https://github.com/jakildev/IrredenEngine/pull/1199
- [x] **T-358** — world: per-frame upload-bandwidth cap + low-LOD billboard metadata (E4) · Owner: claude/T-358-one-frame-upload-budget · PR: https://github.com/jakildev/IrredenEngine/pull/1184
- [x] **T-359** — world: entity chunk migration system (Epic E E5) · Owner: claude/T-359-entity-chunk-migration · PR: https://github.com/jakildev/IrredenEngine/pull/1183
- [x] **T-357** — world: camera-aware chunk prefetch (priority by visibility) (E3) · Owner: claude/T-357-camera-chunk-prefetch · PR: https://github.com/jakildev/IrredenEngine/pull/1180
- [x] **T-366** — fleet: harden cross-host duplicate-claim prevention · Owner: claude/T-366-fleet-duplicate-claiming · PR: https://github.com/jakildev/IrredenEngine/pull/1189
- [x] **T-356** — world: GPU chunk residency manager (LRU + camera-radius eviction) (E2) · Owner: claude/T-356-gpu-chunk-residency · PR: https://github.com/jakildev/IrredenEngine/pull/1179
- [x] **T-363** — tools: ir-host-probe survives non-exec lspci stub in PATH · Owner: claude/T-363-ir-host-probe-harden · PR: https://github.com/jakildev/IrredenEngine/pull/1177
- [x] **T-364** — render: retire C_CameraYaw — camera rotation sources from C_LocalTransform · Owner: claude/T-364-camera-so3-retire-yaw · PR: https://github.com/jakildev/IrredenEngine/pull/1176
- [x] **T-352** — render: fix zoom=16 GL_INVALID_VALUE at glBindImageTexture on Linux/OpenGL · Owner: claude/T-352-zoom16-bind-image-fix · PR: https://github.com/jakildev/IrredenEngine/pull/1174
- [x] **T-365** — fleet: smoke-only mode — persistent cross-host smoke worker · Owner: claude/T-365-smoke-only-mode · PR: https://github.com/jakildev/IrredenEngine/pull/1173
- [x] **T-360** — world: markChunkDirty API + chunk-mutation routing contract · Owner: claude/T-360-chunk-mark-dirty-api · PR: https://github.com/jakildev/IrredenEngine/pull/1172
- [x] **T-355** — docs: T-189/T-190 disposition under SDF restriction (D4) · Owner: claude/T-355-t189-t190-disposition · PR: https://github.com/jakildev/IrredenEngine/pull/1168
- [x] **T-354** — render: SHAPES authoring deprecation migration plan (D3) · Owner: claude/T-354-shapes-deprecation-migration-plan · PR: https://github.com/jakildev/IrredenEngine/pull/1167
