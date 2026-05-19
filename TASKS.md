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



- [ ] **asset: BinaryWriter/Reader + chunk-table header + JSON sidecar emitter** — extend engine/asset/ with shared binary-I/O primitives for all new asset formats (.vxs, .rig, world snapshot)
  - **ID:** T-166
  - **Area:** engine/asset
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) BinaryWriter + BinaryReader (file + memory backends) in binary_io.hpp with full primitive set (U8/U16/U32/U64/I*/F32/F64, varUInt, bytes, string) little-endian, Result<T> on reads; (2) chunk_header.hpp: 12-byte magic+version+chunk-count header + chunk-table entry {tag[4], uint64 offset, uint64 size}; unknown chunks exposed as span<uint8_t>; (3) name_table.hpp: (uint32 numeric_id, string name) pairs for forward-compat enum round-trip; (4) json_sidecar.hpp: write-only flat-object/array emitter, no third-party JSON dep; (5) unit tests: round-trip primitives, varint edges, truncated reads, bad magic, version-too-new, unknown-chunk-tag, name-table round-trip; (6) engine/asset/CLAUDE.md documents the seven Save Format Extensibility Rules + new primitives; (7) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #663
  - **Notes:** Foundation for .vxs, .rig, and world-snapshot formats (T-167, T-168, T-169, #667). Blocker #662 (doc rename) merged as PR #673. No functional consumer until T-167+. World-snapshot (#667) lives in engine/world/ and consumes these primitives; engine/asset/ must not depend on engine/entity/ or engine/world/.
  - **Links:**


- [ ] **render: SDF→trixel half-voxel / lone-trixel discrepancy investigation** — reproduce, classify, and fix or document the artifact difference between C_VoxelSetNew voxel-pool output and direct-SDF SHAPES_TO_TRIXEL rasterization at silhouette boundaries
  - **ID:** T-190
  - **Area:** engine/render, shaders/glsl
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) diff report comparing voxel-pool vs SDF output at zoom 4/8/16 for box, sphere, cone, and torus shapes using tools/img_diff; (2) either a fix PR that eliminates the trixel discrepancy, or a CLAUDE.md note in engine/render/ documenting the intentional delta and its source (which threshold, which solver path); (3) fleet-build clean on linux-debug
  - **Issue:** #690
  - **Notes:** Human observation from PR #659 (T-163 stateless particle render): SDF path emits half-extent trixels or isolated single-trixel artifacts at silhouette boundaries that the voxel-pool path does not produce for the same shape. Investigate: (a) off-by-one from kSdfBiasEpsilon or stableCeilToInt ceiling bias at borderline depths; (b) 2x3 trixel diamond emit painting both subpixels when only one should fire near edge cases; (c) bug in snapLatticeWalk vs findSurfaceDepth. Focus: c_shapes_to_trixel.glsl (boxDepthIntersect/sphereDepthIntersect/snapLatticeWalk) vs c_voxel_to_trixel_stage_1.glsl (localIDToFace_2x3/faceOffset_2x3 emit). The snap mode (subdivisions==1) is designed to match C_VoxelSetNew trixel-for-trixel — divergence there is more likely a bug than intentional.
  - **Links:**

- [ ] **fleet: resolve PR #767 design decisions + rebase cross-machine claim layer** — opus picks direction on 3 fleet-arch decisions (T-138 vs gh_acquire redundancy, cleanup --gh home, label-defs location) then rebases PR #767 to compile cleanly on master
  - **ID:** T-217
  - **Area:** docs/agents/FLEET.md, scripts/fleet/, .claude/commands/
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) PR #767 (or replacement) rebased on master with no semantic conflicts; (2) design decisions implemented: defense-in-depth claim redundancy kept (T-138 rollback + gh_acquire both active), cleanup --gh moved to fleet-queue-tick, label defs (fleet:claim-host-agent, fleet:reviewing-host-agent, fleet:placeholder) moved to FLEET.md §"Issue/PR labeling discipline"; (3) multiple concurrent queue-manager agents running simultaneously is not a problem (race-safe); (4) scripts/fleet/fleet-claim conflicts from master-vs-767 resolved; (5) fleet scripts pass smoke check on linux-debug
  - **Issue:** #774
  - **Notes:** PR #767 was labeled fleet:semantic-conflict by the merger; opus-worker deferred 3 design decisions to human. Human comment directs: keep defense-in-depth (both T-138 + gh_acquire), move cleanup --gh into fleet-queue-tick, new labels into FLEET.md not CLAUDE.md. Ensure multiple queue-manager instances running concurrently is safe. Opus must pick and implement the full solution.
  - **Links:**

- [~] **asset: .vxs DENSE-RLE chunk variant (B3)** — new `VOXR_RLE` chunk tag in .vxs format; RLE encoding reduces hollow 64³ voxel set to ~10% of DENSE chunk size
  - **ID:** T-276
  - **Area:** engine/asset
  - **Model:** sonnet
  - **Owner:** claude/T-276-vxs-rle-chunk
  - **Blocked by:** (none)
  - **Acceptance:** (1) hollow 64³ voxel set saves at ~10% of DENSE chunk size; (2) round-trip unit tests in `engine/asset/tests/` cover empty/full/hollow/striped cases; (3) format extensibility rules verified: old loader skips new chunk silently, new loader prefers RLE; (4) `engine/asset/CLAUDE.md` documents the new chunk tag; (5) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #940
  - **Notes:** Epic B (#935) independent task — no format version bump; old loaders silently skip VOXR_RLE. Hard dependency for Epic E E6 (#938 chunk disk persistence). Must land before Phase 1 authoring (#604) generates dense .vxs corpus. Plan ref: `.claude/plans/okay-lets-go-through-idempotent-giraffe.md` §"Epic B → B3".
  - **Links:**

- [~] **render: runtime-sized voxel pools (B4)** — replace compile-time `kVoxelPoolSize`/`kVoxelPoolMaxAllocationSize` constants with runtime values sized from GPU VRAM budget at startup
  - **ID:** T-277
  - **Area:** engine/render, engine/common
  - **Model:** opus
  - **Owner:** claude/T-277-runtime-voxel-pools
  - **Blocked by:** (none)
  - **Acceptance:** (1) launch with `--voxel-pool-size 128` runs at 128³; default behaviour identical to today at 64³; (2) `render_manager` init logs the selected pool size; (3) all existing demos continue to pass at default; (4) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #941
  - **Notes:** Epic B (#935) independent task — blocks Epic E E2 (#938 GPU residency manager). Replaces `ir_constants.hpp:54,63` TODOs. CLI override via `--voxel-pool-size N`. Sane fallback defaults preserve today's 64³ behaviour. Plan ref: `.claude/plans/okay-lets-go-through-idempotent-giraffe.md` §"Epic B → B4".
  - **Links:**

- [ ] **engine: C_LocalTransform (SQT) component (C1)** — new `C_LocalTransform` component carrying scale/rotation/translation at `engine/prefabs/irreden/common/components/component_local_transform.hpp`; identity-default = today's behaviour
  - **ID:** T-279
  - **Area:** engine/prefabs/irreden/common
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `C_LocalTransform` landed; identity-default = pure data per cpp-ecs §"Component method tiers"; (2) existing scenes render identically (identity transform is a no-op); (3) entity spawned with non-identity SQT renders at transformed position/orientation/scale (verified in inline test demo); (4) no `getComponent` calls in tick functions touch this component; (5) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #943
  - **Notes:** Epic C (#936) foundation task — base of both Stack S-C-core (C1 → C2 → C3 → C7) and S-C-math (C1 → C5 → C4 → C8). `C_Rotation` (Euler vec3, component_rotation.hpp:8-20) is deprecated and removed in a follow-up after consumer migration audit. Plan ref: `.claude/plans/okay-lets-go-through-idempotent-giraffe.md` §"Epic C → C1".
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-281** — render: C_ShapeDescriptor usage audit + docs/design/sdf-runtime-audit.md (D1) · Owner: claude/T-281-sdf-runtime-audit · PR: https://github.com/jakildev/IrredenEngine/pull/982
- [x] **T-280** — world streaming design doc (E0) · Owner: claude/T-280-world-streaming-design · PR: https://github.com/jakildev/IrredenEngine/pull/981
- [x] **T-283** — fleet: filter fleet:epic in project_queue_manager_ingest · Owner: claude/T-283-epic-filter-projector · PR: https://github.com/jakildev/IrredenEngine/pull/980
- [x] **T-282** — fleet: invalidate seen-hash on ingest lock-bail · Owner: claude/T-282-ingest-lock-bail-hash-invalidate · PR: https://github.com/jakildev/IrredenEngine/pull/978
- [x] **T-275** — render IRProfile ScopeTimer + per-stage CPU timing (B0) · Owner: claude/T-275-profile-scope-timer · PR: https://github.com/jakildev/IrredenEngine/pull/977
- [x] **T-278** — editor AABB box-fill + line-fill + face-fill (A1) · Owner: claude/T-278-fill-tools · PR: https://github.com/jakildev/IrredenEngine/pull/976
- [x] **T-215** — editor F-1.5 — save/load round-trip with metadata + JSON sidecar · Owner: claude/T-215-save-load-roundtrip · PR: https://github.com/jakildev/IrredenEngine/pull/933
- [x] **T-213** — editor F-1.3 — layer system panel UI · Owner: claude/T-213-layer-system · PR: https://github.com/jakildev/IrredenEngine/pull/932
- [x] **T-250** — docs: engine/render/CLAUDE.md — fix dead render-baselines pointer, trim catalogs · Owner: claude/T-250-render-claude-md-cleanup · PR: https://github.com/jakildev/IrredenEngine/pull/931
- [x] **T-271** — docs/roles: collapse redundant --repo flags in role-merger.md · Owner: claude/T-271-collapse-repo-flags · PR: https://github.com/jakildev/IrredenEngine/pull/925
- [x] **T-214** — editor F-1.4 — animation scrubber + per-frame undo · Owner: claude/T-214-anim-scrubber-perframe-undo · PR: https://github.com/jakildev/IrredenEngine/pull/928
- [x] **T-274** — docs/roles: decide whether queue-manager produces feedback; document either way · Owner: claude/T-274-queuemanager-feedback · PR: https://github.com/jakildev/IrredenEngine/pull/927
- [x] **T-273** — fleet: verify and document merger.log rotation · Owner: claude/T-273-merger-log-rotation · PR: https://github.com/jakildev/IrredenEngine/pull/926
- [x] **T-270** — docs/roles: catch up architect doc on transient-loop, AMEND, game-repo wrinkle · Owner: claude/T-270-architect-doc-catchup · PR: https://github.com/jakildev/IrredenEngine/pull/923
- [x] **T-269** — docs/roles: adopt fleet-pr-clear-feedback-labels wrapper in sonnet-author + architect · Owner: claude/T-269-clear-feedback-labels-wrapper · PR: https://github.com/jakildev/IrredenEngine/pull/922
- [x] **T-268** — fleet: add fleet:awaiting-base to FLEET.md label dictionary · Owner: claude/T-268-label-drift-fix · PR: https://github.com/jakildev/IrredenEngine/pull/921
- [x] **T-267** — docs/roles: shrink intro boilerplate (Bash rules, cache, repo-slug discovery) to pointers · Owner: claude/T-267-shrink-intro-boilerplate · PR: https://github.com/jakildev/IrredenEngine/pull/920
- [x] **T-266** — docs/roles: invert Engine API removal rule citation (baseline owns it) · Owner: claude/T-266-engine-api-removal-rule · PR: https://github.com/jakildev/IrredenEngine/pull/919
- [x] **T-264** — docs/roles: create FLEET-CROSS-HOST-SMOKE.md · Owner: claude/T-264-cross-host-smoke-doc · PR: https://github.com/jakildev/IrredenEngine/pull/918
- [x] **T-265** — docs/roles: hoist Hard rules into CLAUDE-BASELINE.md + fix broken CRITICAL anchor · Owner: claude/T-265-hoist-hard-rules · PR: https://github.com/jakildev/IrredenEngine/pull/917
