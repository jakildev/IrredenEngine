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

- [~] **editor: F-1.2 — symmetry modes (X/Y/Z mirror, user-set plane offset)** — three independent mirror toggles; each axis has an adjustable mirror-plane offset; mirrored placements fold into the same undo record as the source
  - **ID:** T-212
  - **Area:** creations/editors
  - **Model:** sonnet
  - **Owner:** claude/T-212-symmetry-modes
  - **Blocked by:** (none)
  - **Stack:** T-211..T-215 editor-phase-1
  - **Acceptance:** (1) X-mirror toggle: placing on +X writes a voxel on -X simultaneously with same color/layer/metadata; (2) mirror-plane offset slider adjusts axis live — voxels placed after shift mirror across new axis; (3) stroke crossing the mirror plane writes one voxel per affected cell, not two; (4) all three axes mirrorable independently or combined (verify XYZ octant placement); (5) mirrored placements are part of the same stroke undo record as the source placement; (6) fleet-build clean on linux-debug
  - **Issue:** #762
  - **Notes:** Bounded math; mirrors off the place/erase stroke from F-1.1 (T-211). Part of entity-editor epic #604. See `docs/design/entity-editor-epic.md` §Phase 1.
  - **Links:**

- [~] **editor: F-1.3 — layer system (named voxel groups, visibility toggle)** — each voxel carries a single layer id; layer panel UI with name, color tag, visibility eye, active-layer radio, reorder, add/rename/delete; hidden layers don't pick
  - **ID:** T-213
  - **Area:** creations/editors
  - **Model:** sonnet
  - **Owner:** claude/T-213-layer-system
  - **Blocked by:** (none)
  - **Stack:** T-211..T-215 editor-phase-1
  - **Acceptance:** (1) default Layer 0 exists on empty scene; (2) create new layer → it becomes active → subsequent placements carry its layer id; (3) toggle layer visibility → its voxels hide in viewport AND don't pick; (4) renaming a layer doesn't break per-voxel layer-id references (id stable, name is display-only); (5) reordering layers in panel doesn't change which voxels belong where; (6) deleting a layer moves its voxels to default layer or prompts confirmation; (7) layer membership round-trips through F-1.5 save/load; (8) fleet-build clean on linux-debug
  - **Issue:** #763
  - **Notes:** Layer membership lives in the JSON sidecar (F-0.7) — .vxs v2 binary doesn't need a new field. Decide in implementation whether to store layer-id per voxel in sidecar or as voxel-index ranges per layer. Part of entity-editor epic #604. See `docs/design/entity-editor-epic.md` §Phase 1.
  - **Links:**

- [~] **editor: F-1.4 — frame-based animation (multiple poses, scrubber)** — pixel-art-style frame-by-frame animation; timeline panel with thumbnails, scrubber, play/pause/loop/ping-pong; each frame is an independent voxel-grid snapshot; undo scoped per frame
  - **ID:** T-214
  - **Area:** creations/editors
  - **Model:** sonnet
  - **Owner:** claude/T-214-frame-animation
  - **Blocked by:** (none)
  - **Stack:** T-211..T-215 editor-phase-1
  - **Acceptance:** (1) add frame → new editable snapshot in timeline; switching shows empty or duplicated grid; (2) edit voxels in frame N → frame N+1 unaffected; (3) scrubber drags through frames smoothly, viewport updates per drag tick; (4) play button cycles at configurable FPS (test 6 fps and 24 fps); (5) loop and ping-pong modes both work; (6) frames round-trip through F-1.5 save/load identically; (7) undo (Ctrl-Z) scoped to active frame, doesn't reach into another frame's history; (8) per-frame undo cap documented in impl PR; (9) fleet-build clean on linux-debug
  - **Issue:** #764
  - **Notes:** NOT skeletal animation (that's Phase 3, #606). Each frame is a separate dense .vxs block in v1. Sparse/delta encoding is a Phase 10 perf concern (#613). Risk: per-frame undo cap must be documented to bound memory with many frames. Part of entity-editor epic #604. See `docs/design/entity-editor-epic.md` §Phase 1.
  - **Links:**

- [ ] **editor: F-1.5 — save/load round-trip with metadata + JSON sidecar** — persist editor scene to disk and load it back with exact byte- and behavior-level round-trip
  - **ID:** T-215
  - **Area:** creations/editors, engine/asset
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-213, T-214
  - **Stack:** T-211..T-215 editor-phase-1
  - **Acceptance:** (1) save scene → .vxs v2 + .vxs.json sidecar written to disk; (2) load saved file → editor scene matches exactly (voxel positions, colors, per-voxel metadata, layers, frames, symmetry settings); (3) per-voxel metadata (material_id, flags, bone_id) round-trips byte-exact through binary block; (4) layers round-trip through sidecar (membership, names, visibility, order); (5) frames round-trip (count, content per frame, FPS, loop mode); (6) IRShapeDebug loads the saved .vxs and renders frame 0 correctly; (7) sidecar is human-diffable (deterministic key order, stable indentation, no timestamps)
  - **Issue:** #765
  - **Notes:** Phase 1 F-1.5 save/load acceptance gate for entity-editor epic #604 / umbrella #213. Format support already exists (F-0.6, F-0.7); this wires editor save/load through it. Risk: binary .vxs must carry per-voxel metadata bits — if any field is missing, escalate before extending format (additions go in sidecar, not silent v3 churn). See docs/design/entity-editor-epic.md §Phase 1.
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

- [~] **docs/skills: replace host/preset table copies with refs to BUILD.md** — remove duplicate host/preset tables from render-verify, render-debug-loop, backend-parity
  - **ID:** T-236
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** claude/T-236-host-preset-table-refs
  - **Blocked by:** (none)
  - **Acceptance:** (1) `render-verify/SKILL.md:42-46`, `render-debug-loop/SKILL.md:26-29`, `backend-parity/SKILL.md:253-269` (variant) each replaced with a two-line ref to `docs/agents/BUILD.md`; (2) no duplicate host/preset table remains in these three files
  - **Issue:** #819
  - **Notes:** Follow-up from T-222 audit (§5.7, §1.7). XS. Canonical home is docs/agents/BUILD.md (already referenced from engine-root CLAUDE.md).
  - **Links:**

- [~] **docs/skills: shrink start-next-task's cursor-stack-base coverage; defer mechanism to FLEET.md** — cut ~40 lines from start-next-task/SKILL.md by deferring the cursor-stack-base mechanism description to FLEET.md
  - **ID:** T-237
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** claude/T-237-start-next-task-cursor-stack
  - **Blocked by:** (none)
  - **Acceptance:** (1) `start-next-task/SKILL.md` sections `34-39`, `159-186`, `270-291` trimmed to flow descriptions only; mechanism deferred to `FLEET.md:140-186` ref; (2) net reduction ~40 lines; (3) `commit-and-push/procedures/cursor-stack.md` still correctly scoped to PR-creation deltas only
  - **Issue:** #820
  - **Notes:** Follow-up from T-222 audit (§5.8, §1.5). Size S. Canonical mechanism already in FLEET.md:140-186.
  - **Links:**

- [~] **docs/skills: lift commit-and-push PR-body HEREDOC templates into procedures/pr-body.md** — extract three near-identical PR-body templates into one canonical template with per-mode delta sections
  - **ID:** T-238
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** claude/T-238-pr-body-template
  - **Blocked by:** (none)
  - **Acceptance:** (1) new `.claude/skills/commit-and-push/procedures/pr-body.md` with one canonical PR-body template + single-PR / fleet-stacked / cursor-stacked delta sections; (2) `commit-and-push/SKILL.md:264-374` three templates (~110 lines) replaced with ~2-line refs; (3) net reduction ~50 lines from main SKILL.md
  - **Issue:** #821
  - **Notes:** Follow-up from T-222 audit (§5.13, §1.9). Size M. Three templates differ by only 1-2 fields. Existing `procedures/` pattern: fleet-stack, cursor-stack, rebase-guard.
  - **Links:**

- [~] **docs/skills: lift commit-and-push host-stamp logic into procedures/host-label.md** — extract 37-line host-stamp shell logic into a shared procedures file
  - **ID:** T-239
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** claude/T-239-host-label-procedures
  - **Blocked by:** (none)
  - **Acceptance:** (1) new `.claude/skills/commit-and-push/procedures/host-label.md` with the `fleet:authored-on-{linux,macos}` host-stamp logic; (2) `commit-and-push/SKILL.md:386-423` (~37 lines) replaced with a ref; (3) reviewer-side `review-pr/SKILL.md:478-484` already refs `review-pr/procedures/cross-host-smoke.md` — ensure consistency
  - **Issue:** #822
  - **Notes:** Follow-up from T-222 audit (§5.14, §1.11). Size S. Existing procedures/ pattern: cross-host-smoke.md, cursor-stack.md, rebase-guard.md.
  - **Links:**

- [~] **docs/skills: lift simplify's 98-line serialization version-bump rule into engine/asset/CLAUDE.md** — move asset-format-specific version-bump detection policy out of the generalist simplify skill
  - **ID:** T-240
  - **Area:** docs, engine/asset
  - **Model:** sonnet
  - **Owner:** claude/T-240-simplify-serialization-rule
  - **Blocked by:** (none)
  - **Acceptance:** (1) `simplify/SKILL.md:209-307` (~98 lines) moved to `engine/asset/CLAUDE.md` (or `.claude/rules/cpp-asset-versioning.md`); (2) simplify retains one line: "applies the asset version-bump check from <link>"; (3) rule content (heuristics + false-positive guards + output templates + unannotated-struct extension) preserved verbatim in the new location
  - **Issue:** #823
  - **Notes:** Follow-up from T-222 audit (§5.15, §4.5). Size M. Asset-format policy buried in a generalist skill — wrong layering.
  - **Links:**

- [ ] **docs/skills: rewrite render-trixel-pipeline/SKILL.md to concepts-only; move inventory tables** — reduce structural outlier SKILL.md from inventory-heavy to 30-50 lines of concepts and gotchas
  - **ID:** T-241
  - **Area:** docs, engine/render
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `render-trixel-pipeline/SKILL.md` reduced to 30-50 lines covering concepts and gotchas only; (2) buffer/image binding tables (lines 128-149), `FrameDataVoxelToCanvas` struct copy (153-164), Key Components file-path subsections (166-187), and Render Modes / Shape Types enum tables (189-211) removed or moved; (3) moved concepts filed in `engine/render/CLAUDE.md` if conceptual value exists; (4) SKILL.md gains When-to-invoke / Anti-patterns / Recovery sections per CLAUDE-BASELINE.md structure
  - **Issue:** #824
  - **Notes:** Follow-up from T-222 audit (§5.16, §2 inventory row, §4.5). Size L. CLAUDE-BASELINE.md:113-137 forbids inventory-of-names content in SKILL.md files.
  - **Links:**

- [ ] **docs/skills: trim inventory tables from render-debug-loop, backend-parity, optimize** — delete four inventory sections that violate CLAUDE-BASELINE §no-inventories rule
  - **ID:** T-242
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `render-debug-loop/SKILL.md:175-189` Key Files table removed or compressed to a grep pointer; (2) `backend-parity/SKILL.md:99-118` backend C++ file-pairs table removed; (3) `backend-parity/SKILL.md:121-132` shader-prefix convention table removed (duplicate of CLAUDE-BASELINE); (4) `optimize/SKILL.md:142-158` 15-stage GPU pass-name list removed; (5) all removed sections replaced with "see `<file>`" or Grep pointers
  - **Issue:** #825
  - **Notes:** Follow-up from T-222 audit (§5.17, §2 inventory row). Size M. All four sections list names that are grep-discoverable in the code.
  - **Links:**

- [ ] **docs/skills: standardize SKILL.md structure; drop redundant 'When to invoke' and 'Why this exists' sections** — remove boilerplate intro paragraphs and trigger restatements from 9+ SKILL.md files
  - **ID:** T-243
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) all affected SKILL.md files follow: front-matter → one-sentence body intro → Flow; (2) "When to invoke" sections that restate front-matter `description:` trigger phrases removed from: `simplify:25-38`, `review-pr:23-31`, `commit-and-push:22-28`, `polish-checkpoint:26-35,50-55`, `optimize:42-68`, `backend-parity:37-50`, `start-next-task:46-70`, `attach-screenshots:25-39`, `request-re-review:17-29`, `lua-creation-setup:13-25`; (3) "Why this exists" sections paraphrasing the description removed from the same files
  - **Issue:** #826
  - **Notes:** Follow-up from T-222 audit (§5.18, §4.1). Size M. 9+ of 16 SKILL.md files open with a paragraph paraphrasing front-matter; standardizing cuts significant boilerplate.
  - **Links:**

- [ ] **docs/skills: drop decorative ❌ emoji bullets from Anti-patterns sections** — switch all Anti-patterns / "What this skill does NOT do" sections across SKILL.md files from ❌ bullets to bare list bullets
  - **ID:** T-244
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) every Anti-patterns and "What this skill does NOT do" section in all SKILL.md files uses bare `-` bullets, not ❌; (2) no other decorative emoji bullets remain in SKILL.md files; (3) engine CLAUDE.md and baseline remain unchanged
  - **Issue:** #827
  - **Notes:** Follow-up from T-222 audit (§5.19, §4.6). XS mechanical change. Engine CLAUDE.md and baseline already avoid emoji; SKILL.md files should follow the same convention.
  - **Links:**

- [ ] **docs/skills: compose request-re-review against commit-and-push and start-next-task instead of restating** — reduce request-re-review/SKILL.md to the parts only it owns; invoke the other two skills by reference
  - **ID:** T-245
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `request-re-review/SKILL.md:53-69` staging-discipline subset replaced with "invoke commit-and-push"; (2) `request-re-review/SKILL.md:87-99` branch-release subset replaced with "invoke start-next-task"; (3) skill retains only: trigger discipline, the label swap (remove `human:needs-fix`, add `fleet:changes-made`), and composition instructions
  - **Issue:** #828
  - **Notes:** Follow-up from T-222 audit (§5.21, §3.5). Size S. If commit-and-push or start-next-task contracts tighten, the restated copies silently drift.
  - **Links:**

- [ ] **docs/skills: sweep stale tooling/version refs flagged in audit-skills §4.2** — verify and fix 8 hardcoded refs that will rot: stale Opus stamp, dead doc path, personal username, task ID citation, PR forward-ref, unmerged-hedge, stale cmake command, stale live-deviations list
  - **ID:** T-246
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `review-pr/SKILL.md:393` stale "Opus 4.6" stamp updated or removed; (2) `review-pr/SKILL.md:295-297` `cmake --build ... format-check` replaced with `fleet-build --target format-changed`; (3) `review-pr/SKILL.md:36` dead `docs/AGENT_FLEET_SETUP.md` pointer replaced with `docs/agents/FLEET.md`; (4) `backend-parity/SKILL.md:265-269` personal username (`C:/Users/evinj/...`) removed; (5) `simplify/SKILL.md:155-160` "IRMath::kPi may not be merged" hedge removed (verified merged); (6) `simplify/SKILL.md:189-200` hardcoded live-deviations file:line list removed or made grep-pointer; (7) `lua-creation-setup/SKILL.md:255-257` T-106 task ID citation removed; (8) `render-debug-loop/SKILL.md:122-125` forward-ref to PR #433 removed
  - **Issue:** #829
  - **Notes:** Follow-up from T-222 audit (§5.22, §4.2). Size S. Each ref must be verified against current codebase state before replacing.
  - **Links:**

- [ ] **docs/skills: sweep Bash-rule violations in skill-prescribed snippets** — replace compound-command and process-substitution forms in two SKILL.md files with single-command or tool-based equivalents
  - **ID:** T-247
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `polish-checkpoint/SKILL.md:74` `git diff --stat && git diff` replaced with two separate Bash tool calls or Read/Glob equivalent; (2) `backend-parity/SKILL.md:158-160` `diff <(ls ...) <(ls ...)` replaced with single-command or Glob-based form; (3) `backend-parity/SKILL.md:254,260` raw `cmake --build ... -j$(nproc)` replaced with `fleet-build` (coordinate with T-235)
  - **Issue:** #830
  - **Notes:** Follow-up from T-222 audit (§5.23, §2 Bash rules row). XS. CLAUDE-BASELINE.md:180-219 forbids compound `&&`, process substitution, and `$(nproc)`. The cmake violations overlap with T-235 (#818) — coordinate to avoid double-fixing.
  - **Links:**

- [ ] **docs/skills: trim Anti-patterns sections that restate flow-step requirements** — remove redundant anti-pattern bullets from 6 SKILL.md files, keeping only non-obvious gotchas
  - **ID:** T-248
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) each of the 6 affected skills has ≤2 non-obvious anti-patterns; (2) duplicates of flow-step requirements removed from: `backend-parity/SKILL.md`, `start-next-task/SKILL.md:352-387`, `attach-screenshots/SKILL.md`, `polish-checkpoint/SKILL.md:177-189`, `commit-and-push/SKILL.md:448-464`, `review-pr/SKILL.md:495-505`
  - **Issue:** #831
  - **Notes:** Follow-up from T-222 audit (§5.24, §4.4). Size S. Guideline: keep only anti-patterns that would surprise a reader — things that aren't already obvious from reading the flow steps.
  - **Links:**

- [ ] **docs/skills: pull pipeline-ordering (INPUT -> UPDATE -> RENDER) into one canonical doc** — move the INPUT->UPDATE->RENDER pipeline ordering description to its canonical home; replace 3 SKILL.md restatements with one-line refs
  - **ID:** T-249
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) canonical pipeline-ordering paragraph lives in `engine/system/CLAUDE.md` (or `engine/CLAUDE.md`); (2) `ecs-prefab-creator/SKILL.md:189-196`, `create-creation/SKILL.md:172-181`, `midi-scene-creator/SKILL.md:80-91` each reference the canonical doc instead of restating
  - **Issue:** #832
  - **Notes:** Follow-up from T-222 audit (§5.25, §1.8). XS. `engine/system/CLAUDE.md` already has load-bearing pipeline information; this is the natural home.
  - **Links:**

- [ ] **docs: engine/render/CLAUDE.md — fix dead render-baselines pointer, trim catalogs** — remove dead directory reference, resolve placeholder task ID, and delete function-name and component-name catalog sections
  - **ID:** T-250
  - **Area:** docs, engine/render
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `engine/render/CLAUDE.md:240` dead `engine/render/tests/render-baselines/` pointer fixed (point at real location or delete); (2) `L344` `T-09Y` placeholder resolved to real task ID or removed; (3) `L13-30` `IRRender::` function-name catalog removed; (4) `L119-131` `C_*` component catalog removed; (5) `L41-56` `C_GizmoHandle` per-field docs removed (belong in header); (6) `L137-143` shader naming prefix restatement trimmed to pointer to CLAUDE-BASELINE; (7) pipeline ASCII block at L254-268 preserved
  - **Issue:** #833
  - **Notes:** From T-223 audit (audit-claude-md.md). The render-baselines dead ref is high-priority — PR authors following the instruction will fail. Companion: render-debug-loop SKILL.md references the path inconsistently too.
  - **Links:**

- [ ] **docs: engine/math/CLAUDE.md — collapse function-signature catalogs** — compress the five catalog sections (~60 lines) to gotcha paragraphs; settle the iso-equations canonical home; trim GLM alias restatement
  - **ID:** T-251
  - **Area:** docs, engine/math
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `L37-95` sections (Layout helpers, Color, Physics, Quaternions, Random) each compressed to ≤3-line gotcha paragraph; (2) `L8-12` GLM alias rule trimmed to one sentence + pointer to CLAUDE-BASELINE + cpp-math.md; (3) iso-equations in `L14-36` — pick one canonical home (engine/math/CLAUDE.md) and delete the duplicate from `.claude/rules/cpp-math.md` (or vice versa); (4) PlaneIso axis-swap, SMOOTH mode position-multiplier, and IREasingFunctions gotchas preserved
  - **Issue:** #834
  - **Notes:** From T-223 audit (audit-claude-md.md). The bulk of the file is essentially `ls` of the math headers — violates CLAUDE-BASELINE §"What belongs in CLAUDE.md files".
  - **Links:**

- [ ] **docs: engine/prefabs/CLAUDE.md — de-dup vs .claude/rules/cpp-ecs.md** — make engine/prefabs/CLAUDE.md the canonical home for component-method rules; remove duplicates from the rule file
  - **ID:** T-252
  - **Area:** docs, engine/prefabs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `engine/prefabs/CLAUDE.md:93-147` component-method rules (§(a)/(b)/(c) + Documented exceptions) kept as canonical; (2) near-verbatim duplicate removed from `.claude/rules/cpp-ecs.md` with one-line reference back; (3) `engine/prefabs/CLAUDE.md:150-173` anti-pattern items 1,2,6,7 trimmed (restate cpp-ecs.md rules); (4) IRMath and getComponent restatement lines trimmed; (5) `L40-52` Layout section (directory inventory) deleted; (6) "File pattern" table at L60-66 and cross-domain anti-patterns L155-163 preserved
  - **Issue:** #835
  - **Notes:** From T-223 audit (audit-claude-md.md). CLAUDE-BASELINE.md:74-79 explicitly names engine/prefabs/CLAUDE.md as canonical home for the categorization. The C_PeriodicIdle example and C_VoxelSetNew exception are identical word-for-word in both files.
  - **Links:**

- [ ] **docs: engine/prefabs/irreden/ — prune name catalogs across subtree** — triage all 7 CLAUDE.md files under engine/prefabs/irreden/, keeping only genuine gotchas and pruning name-catalog bullets
  - **ID:** T-253
  - **Area:** docs, engine/prefabs/irreden/common, engine/prefabs/irreden/render, engine/prefabs/irreden/audio, engine/prefabs/irreden/input, engine/prefabs/irreden/update, engine/prefabs/irreden/voxel, engine/prefabs/irreden/video
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `common/CLAUDE.md:7-28` pruned to entries documenting non-obvious lifecycle; `SYSTEM_PROPAGATE_TRANSFORM` banner drift at L74/L113/L153 fixed to `PROPAGATE_TRANSFORM`; (2) `render/CLAUDE.md:8-68` catalog and L41-56 per-field docs trimmed; L70 pipeline header fixed; L351-354 SystemName rule removed; (3) `audio/CLAUDE.md:9-37` catalogs compressed to C_MidiNote::onDestroy() gotcha; "Commands: None" deleted; WIP note for system_audio_device_manager.hpp added; (4) `input/CLAUDE.md:8-26` catalog pruned; beginTick/beforeTick inconsistency resolved; (5) `update/CLAUDE.md:7-17` pruned to C_Velocity3D gotcha; (6) `voxel/CLAUDE.md:8-40` pruned to pool/layout/deprecation notes; stubs at L46-49 and L167-173 moved out; (7) `video/CLAUDE.md` collapsed to "Status: mostly placeholder" + Gotchas
  - **Issue:** #836
  - **Notes:** From T-223 audit (audit-claude-md.md). Size L — per-entry judgment, not a sed pass. Keep entries that document non-obvious lifecycle, modifier-field routing, pool layout, etc.
  - **Links:**

- [ ] **docs: engine/audio + engine/video CLAUDE.md — remove dead pointers** — delete confirmed-absent component names from engine/video/CLAUDE.md and deduplicate engine/audio/CLAUDE.md key-components catalog
  - **ID:** T-254
  - **Area:** docs, engine/audio, engine/video
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `engine/video/CLAUDE.md:87-89` refs to `C_FramebufferCapture`, `C_FramebufferOutputPosition`, `C_OutputResolution` removed or marked "(planned: not yet implemented)"; (2) `engine/video/CLAUDE.md:86-90` cleaned; (3) `engine/audio/CLAUDE.md:57` `C_AudioFile` dead ref removed or marked "(planned)"; (4) `engine/audio/CLAUDE.md:50-58` key-components catalog removed with pointer to `engine/prefabs/irreden/audio/CLAUDE.md`; (5) NOTE: `C_FramebufferCapture` in `engine/prefabs/irreden/video/CLAUDE.md` is valid and untouched
  - **Issue:** #837
  - **Notes:** From T-223 audit (audit-claude-md.md). S — 2 files, ~10 lines changed. The dead C_Framebuffer* names exist only in engine/video/CLAUDE.md; the live version is in the prefab subtree.
  - **Links:**

- [ ] **docs: engine/input/CLAUDE.md — fix C_Hitbox2D dead reference** — replace dead C_Hitbox2D name with actual C_HitboxRect / C_HitboxCircle; verify callback-path consistency; consolidate Lua callback lifetime gotcha
  - **ID:** T-255
  - **Area:** docs, engine/input
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `engine/input/CLAUDE.md:62-65` `C_Hitbox2D` replaced with `C_HitboxRect` / `C_HitboxCircle`; (2) hover-callback section around L62-80 updated to match actual component names and confirmed callback-path (`onHovered`/`onUnhovered`/`onClicked` fire from HitboxRect/HitboxCircle paths); (3) `L72-91` Lua callback lifetime gotcha consolidated to canonical home (`engine/script/CLAUDE.md`) with pointer; (4) Grep confirms no other doc/skill/role files reference `C_Hitbox2D`
  - **Issue:** #838
  - **Notes:** From T-223 audit (audit-claude-md.md). S — one file but needs callback-path verification before fixing. C_HitboxRect and C_HitboxCircle live under `engine/prefabs/irreden/update/components/`.
  - **Links:**

- [ ] **docs: small modules — delete inline directory trees from CLAUDE.md files** — remove 5 ASCII directory-tree blocks that violate CLAUDE-BASELINE §"Do NOT include: File/directory tree listings"
  - **ID:** T-256
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `CLAUDE.md:71-82` root ASCII tree deleted; (2) `engine/CLAUDE.md:20-44` layer-map ASCII tree deleted (keep surrounding prose); (3) `engine/common/CLAUDE.md:38-46` internal-layout tree deleted (keep "header-only, no src/" note); (4) `engine/utility/CLAUDE.md:27-33` ASCII tree deleted entirely; (5) `engine/profile/CLAUDE.md:54-64` internal-layout section deleted (already proven stale — omits profile_report.hpp)
  - **Issue:** #840
  - **Notes:** From T-223 audit (audit-claude-md.md). XS — 5 mechanical deletions, no judgment calls. CLAUDE-BASELINE.md forbids directory tree listings: "Agents can Glob/Grep."
  - **Links:**

- [ ] **docs: engine/system/CLAUDE.md ↔ .claude/rules/cpp-systems.md de-dup** — make engine/system/CLAUDE.md canonical for tick-signature and static-anti-pattern rules; remove verbatim duplicates from the rule file
  - **ID:** T-257
  - **Area:** docs, engine/system
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `engine/system/CLAUDE.md:56-82` (three valid TICK signatures) kept as canonical; verbatim duplicate removed from `.claude/rules/cpp-systems.md` with one-line reference; (2) `engine/system/CLAUDE.md:210-227` (static anti-pattern) kept as canonical; duplicate removed from rule file; (3) `engine/system/CLAUDE.md:10-14` `IRSystem::` function-name catalog removed; (4) `engine/system/CLAUDE.md:228-231` dead `.fleet/status/system-static-deviations.md` pointer removed
  - **Issue:** #841
  - **Notes:** From T-223 audit (audit-claude-md.md). S — pick one canonical home for each rule, delete the other side. The three-function-signatures block and static anti-pattern explanation are duplicated verbatim.
  - **Links:**

- [ ] **docs: creations/ + creations/demos/ CLAUDE.md — fix drift** — resolve stale voxel_editor reference, remove .gitignore paste, trim redundant CMake walkthrough, fix incomplete demo inventory
  - **ID:** T-258
  - **Area:** docs, creations
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `creations/CLAUDE.md:18,29-34` voxel_editor refs resolved (add "in flight under T-211" note if path doesn't exist, else leave); (2) `L23-34` .gitignore paste replaced with one-liner pointer; (3) `L77-95` CMake boilerplate section trimmed to MinGW-DLL gotcha + shape_debug pointer; (4) `L14-21` tree of creations/ subdirs replaced with prose; (5) `creations/demos/CLAUDE.md:16-48` demo inventory replaced with 2-line summary naming only canonical reference demos (shape_debug, default, lua_perf_grid); (6) `L51-67` adding-a-new-demo recipe compressed to one sentence
  - **Issue:** #842
  - **Notes:** From T-223 audit (audit-claude-md.md). M — mechanical but with one judgment call: whether creations/editors/voxel_editor/ exists on disk. T-211 is in flight and will create it; check via Glob before deciding.
  - **Links:**

- [ ] **docs: SQT transition notes across prefabs/ family CLAUDE.md** — add in-flight T-199 transition notes to 3 CLAUDE.md files that reference legacy C_Position3D without acknowledging the ongoing migration
  - **ID:** T-259
  - **Area:** docs, engine/prefabs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `engine/prefabs/irreden/common/CLAUDE.md:9-18` softened from "retired" to "in flight under T-199 — superseded by C_LocalTransform + C_WorldTransform when consumers migrate"; (2) `engine/prefabs/irreden/render/CLAUDE.md:42,48,209,213-218` code examples using C_Position3D get a one-line transition note pointing at T-199; (3) `engine/prefabs/CLAUDE.md:94-97` common/ section notes that both legacy and new SQT components coexist during T-199 migration; (4) engine/script/CLAUDE.md Lua examples left unchanged (addressed when T-199 lands the Lua-side migration)
  - **Issue:** #843
  - **Notes:** From T-223 audit (audit-claude-md.md). XS — 3 small inserts/edits. T-199 is still in-flight ([~]); this task adds accurate in-flight documentation rather than waiting for migration to complete.
  - **Links:

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-234** — docs/skills: shrink commit-and-push cross-repo info-isolation procedure to a check + baseline ref · Owner: claude/T-234-shrink-cross-repo-isolation · PR: https://github.com/jakildev/IrredenEngine/pull/879
- [x] **T-235** — docs/skills: consolidate fleet-build/fleet-run snippets into one canonical block in BUILD.md · Owner: claude/T-235-build-snippets · PR: https://github.com/jakildev/IrredenEngine/pull/876
- [x] **T-207** — script: re-remove IrredenEngineRendering from engine/script/CMakeLists.txt · Owner: claude/T-207-script-remove-render-link · PR: https://github.com/jakildev/IrredenEngine/pull/860
- [x] **T-233** — docs/skills: replace naming-table copies with one-line refs to CLAUDE-BASELINE · Owner: claude/T-233-drop-naming-table-copies · PR: https://github.com/jakildev/IrredenEngine/pull/859
- [x] **T-221** — docs audit of role-*.md — shared protocols + point-don't-dump · Owner: claude/T-221-roles-audit · PR: https://github.com/jakildev/IrredenEngine/pull/857
- [x] **T-211** — editor F-1.1 — place/erase + palette panel + undo stack · Owner: claude/T-211-place-erase-palette-undo · PR: https://github.com/jakildev/IrredenEngine/pull/785
- [x] **T-232** — docs/skills: move IRMath substitution table into .claude/rules/cpp-math.md · Owner: claude/T-232-irmath-table-to-rules · PR: https://github.com/jakildev/IrredenEngine/pull/853
- [x] **T-231** — docs/skills: extract ECS-invariants checklist into .claude/rules/cpp-ecs-smells.md · Owner: claude/T-231-ecs-smells-rules-file · PR: https://github.com/jakildev/IrredenEngine/pull/852
- [x] **T-230** — docs/skills: reconcile backend-parity chaining vs start-next-task no-auto-invoke · Owner: claude/T-230-backend-parity-no-chain · PR: https://github.com/jakildev/IrredenEngine/pull/851
- [x] **T-229** — docs/skills: pick a single formatter owner · Owner: claude/T-229-formatter-owner · PR: https://github.com/jakildev/IrredenEngine/pull/850
- [x] **T-228** — docs/skills: align model-version stamps emitted by review-pr and commit-and-push · Owner: claude/T-228-align-model-version-stamps · PR: https://github.com/jakildev/IrredenEngine/pull/848
- [x] **T-227** — docs/skills: unify --auto-screenshot contract symbol names across render-debug-loop and render-verify · Owner: claude/T-227-auto-screenshot-symbol-unify · PR: https://github.com/jakildev/IrredenEngine/pull/847
- [x] **T-206** — voxel: route C_VoxelSetNew pool calls through IRPrefab::VoxelPool · Owner: claude/T-206-voxel-pool-api · PR: https://github.com/jakildev/IrredenEngine/pull/846
- [x] **T-226** — docs/skills: add missing name: field to request-re-review/SKILL.md front-matter · Owner: claude/T-226-request-re-review-name-field · PR: https://github.com/jakildev/IrredenEngine/pull/845
- [x] **T-225** — docs/skills: fix cross-repo info-isolation leaks in midi-scene-creator and create-creation · Owner: claude/T-225-fix-cross-repo-isolation-leaks · PR: https://github.com/jakildev/IrredenEngine/pull/844
- [x] **T-224** — render/picking: drop dead SDF box guard + unreachable flatIdx check; port 4 extra tests · Owner: claude/T-224-drop-dead-guards · PR: https://github.com/jakildev/IrredenEngine/pull/839
- [x] **T-223** — docs: audit CLAUDE.md files — baseline drift, dead pointers, slop · Owner: claude/T-223-claude-md-audit · PR: https://github.com/jakildev/IrredenEngine/pull/806
- [x] **T-222** — docs audit of SKILL.md files — shared protocols + point-don't-dump · Owner: claude/T-222-skills-audit · PR: https://github.com/jakildev/IrredenEngine/pull/804
- [x] **T-220** — entity: dedup globalFieldRegistry — return stable FieldBindingId on repeated registerField calls · Owner: claude/T-220-dedup-field-registry · PR: https://github.com/jakildev/IrredenEngine/pull/799
- [x] **T-219** — extend castVoxelRay to walk C_VoxelSetNew entities · Owner: claude/T-219-castvoxelray-voxel-sets · PR: https://github.com/jakildev/IrredenEngine/pull/795
