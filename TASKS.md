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


- [~] **world: GPU chunk residency manager (LRU + camera-radius eviction) (E2)** — implement finite GPU voxel-pool budget divided across resident chunks with LRU + camera-radius eviction and async upload/eviction jobs
  - **ID:** T-356
  - **Area:** engine/world, engine/system
  - **Model:** opus
  - **Owner:** claude/T-356-gpu-chunk-residency
  - **Blocked by:** (none)
  - **Stack:** T-356..T-359 S-E-stream
  - **Acceptance:** (1) With 9× more chunks than budget, camera walk triggers stable evict/upload cycle; (2) no flicker; CPU+GPU profile via B0 infra; (3) resident-set count visible in perf_grid HUD; (4) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #964
  - **Notes:** E2 in Epic E (world streaming, #938). Stack position 3: E0→E1→**E2**→E3→E4. All blockers done: E0 (#944), E1 (#963), B4 (#941), B5 (#952) all closed. Uses B5 push-at-mutation for chunk-content upload on residency flip. Should land before T-360 (chunk persistence follow-ups).
  - **Links:**

- [~] **world: camera-aware chunk prefetch (priority by visibility) (E3)** — queue upload jobs for chunks entering camera reach radius before needed, prioritized by in-frustum visibility
  - **ID:** T-357
  - **Area:** engine/world, engine/system
  - **Model:** opus
  - **Owner:** claude/T-357-camera-chunk-prefetch
  - **Blocked by:** T-356
  - **Stack:** T-356..T-359 S-E-stream
  - **Acceptance:** (1) Slow camera pan = no upload-on-render stalls; (2) warps to POI fully render within warp frame; (3) in-frustum chunks upload before peripheral; (4) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #965
  - **Notes:** E3 in Epic E (#938). Stack position 4: E2→**E3**→E4. Blocked by E2 (T-356).
  - **Links:**

- [~] **world: one-frame upload budget + low-LOD fallback (E4)** — cap per-frame upload bandwidth; render off-budget chunks at low-LOD for one frame with fade-in detail
  - **ID:** T-358
  - **Area:** engine/world, engine/render
  - **Model:** opus
  - **Owner:** claude/T-358-one-frame-upload-budget
  - **Blocked by:** T-357
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
  - **Blocked by:** T-356
  - **Stack:** T-356..T-359 S-E-stream
  - **Acceptance:** (1) Track entity moving across 10 chunk boundaries; ID unchanged; (2) rendering correct throughout; (3) rotated entities (C6 #957) migrate without artifact; (4) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #967
  - **Notes:** E5 in Epic E (#938). Off-stack fork from E2 (does not block E3→E4 chain). Blocked by E2 (T-356). Interacts with C6 GRID-mode rotation (#957, closed) for boundary-straddling rotated entities.
  - **Links:**

- [~] **fleet: investigate + remediate concurrent-fleet duplicate work claiming** — find and fix the gaps that allow two concurrent fleet instances (cross-host or same-host) to pick up the same task, review, or queue-manager slot simultaneously
  - **ID:** T-366
  - **Area:** tooling
  - **Model:** opus
  - **Owner:** claude/T-366-fleet-duplicate-claiming
  - **Blocked by:** (none)
  - **Acceptance:** (1) Root cause documented for each duplicate-claim path (task claim, review claim, queue-manager spawn); (2) fleet-claim or scout/label protocol extended to prevent cross-host races; (3) FLEET.md updated with cross-fleet coordination protocol; (4) existing single-fleet operation unaffected
  - **Issue:** #1182
  - **Notes:** Two fleets on separate hosts both picking up the same work. Existing fleet-claim is git-branch-based but cross-host git push latency creates a race window. Queue-manager: multiple scouts on separate hosts may simultaneously detect the same human:approved issue. Review claiming via labels (fleet:wip, fleet:reviewing) reported as not working for issues. TASKS.md write conflicts possible if both queue-managers commit simultaneously. Investigate: (a) fleet-claim claim-check vs scout tick race; (b) atomicity of fleet:queued label add from multiple hosts; (c) fleet:wip label race for review claims; (d) multi-queue-manager TASKS.md write contention.
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

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
- [x] **T-340** — fleet/merger: rebase + verdict preservation on merged-base re-target · Owner: claude/T-340-merger-merged-base-retarget · PR: https://github.com/jakildev/IrredenEngine/pull/1146
- [x] **T-345** — fleet: fleet-build --target format restricted to touched files · Owner: claude/T-345-fleet-build-format-touched-files · PR: https://github.com/jakildev/IrredenEngine/pull/1145
- [x] **T-339** — fleet: review-pr verdict-label retry-and-verify guard · Owner: claude/T-339-review-pr-verdict-label-retry · PR: https://github.com/jakildev/IrredenEngine/pull/1144
