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


- [~] **world: one-frame upload budget + low-LOD fallback (E4)** — cap per-frame upload bandwidth; render off-budget chunks at low-LOD for one frame with fade-in detail
  - **ID:** T-358
  - **Area:** engine/world, engine/render
  - **Model:** opus
  - **Owner:** claude/T-358-one-frame-upload-budget
  - **Blocked by:** (none)
  - **Stack:** T-356..T-359 S-E-stream
  - **Acceptance:** (1) Camera warp to 50-chunk region: first frame renders complete (some at low-LOD); (2) 2-3 frames upgrade to full detail; (3) no stutter > 1 frame; (4) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #966
  - **Notes:** E4 in Epic E (#938). Stack position 5: E3→**E4**. Blocked by E3 (T-357) and E0 (#944, closed). Load-bearing invariant: no frame ever blocks on upload.
  - **Links:**

- [~] **world: entity chunk migration (atomic ownership transfer) (E5)** — entities crossing chunk boundaries change ownership atomically; identity, component data, and voxel allocations preserved
  - **ID:** T-359
  - **Area:** engine/world, engine/entity
  - **Model:** opus
  - **Owner:** claude/T-359-entity-chunk-migration
  - **Blocked by:** (none)
  - **Stack:** T-356..T-359 S-E-stream
  - **Acceptance:** (1) Track entity moving across 10 chunk boundaries; ID unchanged; (2) rendering correct throughout; (3) rotated entities (C6 #957) migrate without artifact; (4) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #967
  - **Notes:** E5 in Epic E (#938). Off-stack fork from E2 (does not block E3→E4 chain). Blocked by E2 (T-356). Interacts with C6 GRID-mode rotation (#957, closed) for boundary-straddling rotated entities.
  - **Links:**

- [~] **tooling: /increase-complexity skill — auto-grow demos with new engine systems and entity count** — skill scans engine/prefabs and system registrations, proposes and applies additive changes to make a target demo more visually complex
  - **ID:** T-367
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** claude/T-367-increase-complexity-skill
  - **Blocked by:** (none)
  - **Acceptance:** (1) `/increase-complexity` skill exists in `.claude/skills/`; (2) when invoked on a demo, scans available systems/prefabs and appends entities or parameters to increase visual complexity; (3) user can optionally specify what kind of change is wanted; (4) fleet-build clean on linux-debug after applying changes to any touched demo
  - **Issue:** #1064
  - **Notes:** Skill should look for new engine or game systems and include them in the demo. Optional: dry-run mode prints proposed changes before applying.
  - **Links:**

- [ ] **asset: async texture loading via pinned worker (T-226 Phase 5)** — `IRAsset::loadTextureAsync` returns immediately with an AssetHandle; disk read + decode runs on a pinned I/O worker; GL texture upload schedules onto main thread
  - **ID:** T-368
  - **Area:** engine/system
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `IRAsset::loadTextureAsync` exists, returns `AssetHandle<C_Texture>`, produces a valid texture once resolved; (2) one existing blocking startup load migrated to async; (3) startup-time delta filed in `docs/perf-reports/`; (4) no regression on `perf_grid_matrix.sh`
  - **Issue:** #1073
  - **Notes:** Phase 5 of multithreading epic (#226). Blocker #1068 (IRJobs::pinTo) is now closed. GL/Metal texture creation must happen on the main thread — pinned worker does disk-read + decode only; upload schedules onto main. One entry point POC only; other loaders follow if the pattern is right.
  - **Links:**

- [~] **math: add IRMath::cbrt cube-root primitive (extract from perf_grid demo)** — hoist `std::cbrt` call from `perf_grid/main.cpp:532` into `engine/math/` as `IRMath::cbrt<T>` following existing IRMath conventions
  - **ID:** T-369
  - **Area:** engine/math
  - **Model:** sonnet
  - **Owner:** claude/T-369-irmath-cbrt
  - **Blocked by:** (none)
  - **Acceptance:** (1) `engine/math/` exports `IRMath::cbrt` (float/double overloads, `constexpr`, `noexcept`); (2) `perf_grid/main.cpp:532` uses `IRMath::cbrt`; (3) `grep -rn "std::cbrt"` returns zero results outside allowlisted backend/glue; (4) IRMath substitution table updated
  - **Issue:** #1088
  - **Notes:** Triggered by reviewer nit on PR #1081 (T-220). Primary motivation is preventing a second `std::cbrt` consumer before the primitive is hoisted.
  - **Links:**

- [~] **perf: IRPerfGrid UPDATE pipeline — reduce 8.6s/frame to ≤33ms on linux-x86_64** — profile + fix dominant UPDATE systems (PropagateTransform, PeriodicIdle, UpdateVoxelSetChildren) to reach ≥30 FPS on IRPerfGrid
  - **ID:** T-370
  - **Area:** engine/system, engine/world
  - **Model:** opus
  - **Owner:** claude/T-370-perfgrid-update-pipeline
  - **Blocked by:** (none)
  - **Acceptance:** IRPerfGrid frame time on linux-x86_64 ≤ 33 ms with visual parity to master
  - **Issue:** #1161
  - **Notes:** Diagnosed during T-351 by opus-worker. UPDATE ~8.6s/frame vs RENDER ~0.2s/frame at 262k entities. PropagateTransform (99893ms), PeriodicIdle (69567ms), UpdateVoxelSetChildren (45851ms) dominate. Approaches: (1) cap UPDATE catch-up ticks/frame, (2) parallelize disjoint UPDATE systems (PARALLEL_FOR prerequisites T-222/T-224 landed), (3) reduce per-entity work in dominant systems.
  - **Links:**

- [~] **world: chunk persistence — two-level directory split (T-298 follow-up 2/4)** — update `ChunkDiskPersistence::chunkPath` / `filenameForKey` to use `chunks/<x_div_N>/<y_div_N>/` layout before any durable saves exist
  - **ID:** T-371
  - **Area:** engine/world
  - **Model:** sonnet
  - **Owner:** claude/T-371-chunk-persistence-two-level-dir-split
  - **Blocked by:** (none)
  - **Acceptance:** (1) Benchmark validates split dimensions on ext4/NTFS; (2) `chunkPath` and `filenameForKey` updated; (3) tests' expected filename fragments migrated; (4) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #1169
  - **Notes:** Split from #1008 item 2. T-298 (#998) deferred directory layout pending profiling. No migration tooling needed (nothing real persists today). Two-level `<x_div_64>/<y_div_64>` is the working proposal; one-level `x_div_N` may suffice on ext4/NTFS — quick benchmark gates the decision.
  - **Links:**

- [~] **world: chunk persistence — wire in-engine consumer end-to-end (T-298 follow-up 3/4)** — pick one consumer (voxel editor save path or new IRChunkStreamingSmoke demo) and wire a real `ChunkResidencyManager` + `VoxelPoolAllocation` round-trip
  - **ID:** T-372
  - **Area:** engine/world
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-2
  - **Blocked by:** (none)
  - **Acceptance:** (1) Consumer wired and running on linux-debug and macos-debug; (2) chunk file lands under `<save>/chunks/`; (3) round-trip preserves voxel data; (4) clean chunks skip the save
  - **Issue:** #1170
  - **Notes:** Split from #1008 item 3. T-298's code path has no in-engine consumer — only gtest fake-pool integration. Candidates: voxel editor "save chunks" path, or new `IRChunkStreamingSmoke` demo. Should land before E2/E3 (T-357/T-358) so persistence is proven on a real pool.
  - **Links:**

- [~] **world: rename ChunkDiskPersistence → ChunkVoxelDiskPersistence (T-298 follow-up 4/4)** — rename to make explicit the class saves the voxel-pool slice only, not entity manifest or billboard metadata
  - **ID:** T-373
  - **Area:** engine/world
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-1
  - **Blocked by:** (none)
  - **Acceptance:** All `ChunkDiskPersistence` references in engine/ replaced with `ChunkVoxelDiskPersistence`; fleet-build clean on linux-debug and macos-debug
  - **Issue:** #1171
  - **Notes:** Split from #1008 item 4. Low-priority rename pass. May fold into T-371 or T-372 if convenient; standalone skip is fine. Prevents future PRs from assuming entity state is durable via this class.
  - **Links:**

- [~] **tooling: queue-manager — detect stale TASKS.md rows whose scope shipped under a different T-NNN** — before ingesting a `human:approved` issue, search recent merged PRs for the issue number; skip ingest and post a comment if the scope already landed
  - **ID:** T-374
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-1
  - **Blocked by:** (none)
  - **Acceptance:** (1) Ingest pass searches merged PR titles/bodies for the issue number; (2) if a merged PR references the issue, skip ingest and post a comment linking the landing PR; (3) normal ingest path unaffected; (4) T-361/T-362 pattern (sub-task shipped under different T-NNN prefix) is caught
  - **Issue:** #1175
  - **Notes:** Diagnosed by opus-worker-1 — T-361, T-362, T-363 rows appeared in TASKS.md because implementing PRs used different T-NNN prefixes. T-361 and T-362 have since been removed from TASKS.md. Do NOT auto-close source issues (#1055, #1071, #1074) — human decides. Adjacent to T-338 (maintenance-sync) and T-342 (divergence check).
  - **Links:**

- [~] **fleet: fleet-tasks-render — preserve [~] from cross-host fleet:claim-* labels (Bug 1 from #1182)** — add cross-host claim label check to `derive_status()` so maintenance-sync on host B no longer reverts claims held on host A
  - **ID:** T-375
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-1
  - **Blocked by:** (none)
  - **Acceptance:** (1) Claim on host A + fleet-tasks-render on host B → status stays `[~]`; (2) maintenance-sync no longer reverts cross-host claims; (3) pure-local single-host flow unchanged; (4) synthetic test injects `fleet:claim-*` label and asserts `[~]` preserved across render cycle lacking local FS claim
  - **Issue:** #1190
  - **Notes:** Root cause: `fleet-tasks-render:load_fs_claims()` host-local; `derive_status()` preserves `[~]` only when task_id in fs_claims. Fix: also treat live `fleet:claim-<host>-<agent>` label on task's linked issue as `[~]`-preservation signal (one `gh issue list -l "fleet:claim-*"` per render). Repro in T-366/#1182 timeline: claim reverted 54s after acquisition. Duplicate issue: #1186.
  - **Links:**

- [~] **fleet: fleet-claim — TTL sweep stale fleet:claim-* labels off open issues (Bug 2 from #1182)** — extend `cmd_cleanup --gh` to drop `fleet:claim-*` from open issues where holder has gone silent (no PR, no recent commit, past TTL)
  - **ID:** T-376
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** claude/T-376-fleet-claim-ttl-sweep
  - **Blocked by:** (none)
  - **Acceptance:** (1) `fleet-claim cleanup --gh` drops stale `fleet:claim-*` from open issues per TTL rules; (2) active claims (open WIP PR or recent commit) are never swept; (3) idempotent — sweep twice = no-op second time; (4) removal logged in same format as closed-issue sweep; (5) queue-tick calls the sweep so it self-heals
  - **Issue:** #1191
  - **Notes:** Root cause: `cmd_check_stale` sweeps FS claims and `fleet:claim-*` off closed issues only — no pass sweeps open issues. T-366/#1182 held `fleet:claim-mac-opus-worker-2` for 17+ hours after an abandoned empty commit. Drop criteria: no matching PR AND label age > TTL (default 7200s) OR linked task Owner → free. Duplicate issue: #1187.
  - **Links:**

- [~] **fleet: commit-and-push — refuse to commit when staged tree is empty (Bug 3 from #1182)** — add pre-flight check in commit-and-push skill so empty commits can't be pushed; exit non-zero with a release-or-work instruction
  - **ID:** T-377
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-2
  - **Blocked by:** (none)
  - **Acceptance:** (1) `commit-and-push` with no staged changes fails with clear error message; (2) error instructs worker to either stage real work or release the claim; (3) existing non-empty-commit path unaffected; (4) skill docs state the empty-commit guard contract
  - **Issue:** #1192
  - **Notes:** Root cause: commit `85662e24` on `claude/T-366-fleet-duplicate-claiming` pushed empty tree-delta under a task title. Fix: `git diff --cached --quiet` check before `git commit`; exit non-zero if staged tree equals HEAD. Affects SKILL.md and any procedures/*.md with embedded commit paths. Duplicate issue: #1188.
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
  - **Owner:** sonnet-fleet-2
  - **Blocked by:** (none)
  - **Acceptance:** (1) ≥10 additional prefab systems annotated with `PARALLEL_FOR` beyond the 2 already done; (2) `IrredenEngineTest` 100% pass; (3) measurable speedup at 262K entities vs post-Phase-3 baseline; (4) results filed in `docs/perf-reports/threading_bulk_migration.md`; (5) `engine/system/CLAUDE.md` updated with full list of parallelized systems; (6) no regression in `IRShapeDebug --auto-screenshot 60`
  - **Issue:** #1196
  - **Notes:** Mechanical sweep: grep for `registerSystem<`/`createSystem<` across `engine/prefabs/`, build inventory table, classify each system. Safe criteria: per-component tick refs only, no `EntityId` first param, no manager/GL/Metal/sol2/LuaScript calls in body, no static or shared-state access. Likely candidates: ANIMATION_COLOR, ANIMATION_FRAMES, PERIODIC_IDLE, PERIODIC_IDLE_POSITION_OFFSET, LIFETIME, MODIFIER_DECAY, VELOCITY_ROTATION, SCALE_ANIMATION, FADE_IN, FADE_OUT. Check ANIMATION_COLOR — may already be annotated from T-222 POC. Do not reorganize pipeline groups in this PR. Part of epic #226.
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

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
- [x] **T-353** — render: SDF entity rotation via C_WorldTransform quaternion (C8) · Owner: claude/T-353-sdf-entity-rotation · PR: https://github.com/jakildev/IrredenEngine/pull/1166
- [x] **T-350** — docs/role-merger — explain re-target + label cleanup order on stacked-base merged path · Owner: claude/T-350-merger-retarget-order-rationale · PR: https://github.com/jakildev/IrredenEngine/pull/1163
- [x] **T-351** — render: adaptive COMPUTE_LIGHT_VOLUME propagate iteration count · Owner: claude/T-351-compute-light-volume-opt · PR: https://github.com/jakildev/IrredenEngine/pull/1162
- [x] **T-347** — script/codegen: emit per-component tick to unlock PARALLEL_FOR · Owner: claude/T-347-lua-codegen-per-component · PR: https://github.com/jakildev/IrredenEngine/pull/1160
- [x] **T-349** — engine/system: order validator rules most-specific-first for catch-all + PARALLEL_FOR · Owner: claude/T-349-validator-rule-ordering · PR: https://github.com/jakildev/IrredenEngine/pull/1159
- [x] **T-348** — engine/system: SERIAL fast-path + dual-slot consolidation · Owner: claude/T-348-serial-fastpath-dual-slot · PR: https://github.com/jakildev/IrredenEngine/pull/1158
- [x] **T-342** — fleet: queue-manager queued/free divergence check · Owner: claude/T-342-queue-manager-divergence-check · PR: https://github.com/jakildev/IrredenEngine/pull/1148
- [x] **T-344** — fleet/auto-mode: fix rm -f .review-body.md via Read-then-Write protocol · Owner: claude/T-344-auto-mode-allowlist · PR: https://github.com/jakildev/IrredenEngine/pull/1151
- [x] **T-343** — fleet: review-pr live label check after claim acquisition (pre-checkout) · Owner: claude/T-343-review-pr-live-label-check · PR: https://github.com/jakildev/IrredenEngine/pull/1150
- [x] **T-346** — fleet: scout stackable_blocker_pr false-positive filter · Owner: claude/T-346-scout-stackable-filter · PR: https://github.com/jakildev/IrredenEngine/pull/1147
