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

- [~] **engine: migrate position/rotation consumers to C_WorldTransform — retire C_Position3D/C_PositionGlobal3D/C_Rotation** — swap every consuming system from legacy position/rotation components to the new SQT transform pair and delete the retired components
  - **ID:** T-199
  - **Area:** engine/prefabs/irreden/render, engine/prefabs/irreden/update, engine/prefabs/irreden/input, engine/prefabs/irreden/voxel, engine/entity
  - **Model:** opus
  - **Owner:** claude/T-199-step1-clean
  - **Blocked by:** (none)
  - **Stack:** T-197..T-199 transform-consolidation
  - **Acceptance:** (1) `grep -r "C_PositionGlobal3D"` and `grep -r "C_Position3D"` return only references in this ticket's deletion commits; (2) `grep -r "C_Rotation"` cleaned up; (3) every consumer reads `C_WorldTransform` (or `C_LocalTransform` for write paths); (4) `IRShapeDebug` render-debug-loop shot list passes pre/post-migration; (5) IRVoxelEditor/current editor demo functions: gizmos move, voxels position correctly, hitboxes resolve; (6) no regressions in tests: `test/ecs/*`, `test/asset/*`; (7) `engine/prefabs/irreden/common/CLAUDE.md` and `engine/prefabs/irreden/voxel/CLAUDE.md` updated to describe post-migration model; (8) verified on linux-debug (OpenGL) and macos-debug (Metal)
  - **Issue:** #736
  - **Notes:** Part of epic #731 (transform consolidation, Phase 2). Likely too large for one PR — consider splitting by subsystem: render-side → input-side → voxel-side → final retirement. Key gotcha: `C_VoxelPool`'s SoA layout currently carries `{C_Position3D, C_PositionOffset3D, C_PositionGlobal3D}` arrays — decide during impl whether to use one `C_WorldTransform` array or keep position-only views as cached projections. Lua bindings (sol2 + `*_lua.hpp` files) may need updating. GPU-side shape descriptor stays position-only; convert SQT→position on CPU before staging. Animation systems (sprite UV) not affected but audit `C_AnimationClip` / `C_ActionAnimation`.
  - **Links:**

- [~] **script: complete T-188 layering — decouple prefab_api.cpp + shape descriptor from IRRender** — remove the residual `IrredenEngineRendering` link from `engine/script/` by moving `ShapeType`, `getActiveCanvasEntityOrNull`, and voxel pool allocator to render-neutral headers/modules
  - **ID:** T-201
  - **Area:** engine/script, engine/render, engine/prefabs/irreden/voxel, engine/math, engine/world
  - **Model:** opus
  - **Owner:** claude/T-201-lod-level-shape-types-split
  - **Blocked by:** (none)
  - **Acceptance:** (1) fresh `cmake --preset linux-debug` (or `macos-debug`) configure + `fleet-build --target IrredenEngineScripting` builds without `IrredenEngineRendering` in the script link list; (2) `IrredenEngineTest` builds and all `PrefabApi.*` tests pass; (3) `IRShapeDebug` and standard demos build and run; (4) `engine/script/src/prefab_api.cpp` no longer includes `<irreden/ir_render.hpp>`; (5) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #739
  - **Notes:** T-188 (#723) partially decoupled scripting from rendering; T-189 (#729) re-added `IrredenEngineRendering` to `engine/script/CMakeLists.txt` as a temporary workaround after a fresh-rebase build break. Three-step architect refactor: (a) move `IRRender::ShapeType` + `SHAPE_FLAG_*` to a render-neutral header in `engine/math/` or `engine/common/`; (b) decide where `getActiveCanvasEntityOrNull()` lives (move to `engine/world/` or thread canvas ID through callers); (c) refactor `C_VoxelSetNew` so it doesn't directly call `IRRender::allocateVoxels`/`deallocateVoxels`. Likely 2–3 stacked PRs. Pool allocation must stay lean — no virtual indirection in the hot path. `getActiveCanvasEntityOrNull` is on the documented cpp-ecs exceptions list; replacement must keep prefab spawn ergonomic.
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



- [~] **asset: BinaryWriter/Reader + chunk-table header + JSON sidecar emitter** — extend engine/asset/ with shared binary-I/O primitives for all new asset formats (.vxs, .rig, world snapshot)
  - **ID:** T-166
  - **Area:** engine/asset
  - **Model:** opus
  - **Owner:** opus-worker-2
  - **Blocked by:** (none)
  - **Acceptance:** (1) BinaryWriter + BinaryReader (file + memory backends) in binary_io.hpp with full primitive set (U8/U16/U32/U64/I*/F32/F64, varUInt, bytes, string) little-endian, Result<T> on reads; (2) chunk_header.hpp: 12-byte magic+version+chunk-count header + chunk-table entry {tag[4], uint64 offset, uint64 size}; unknown chunks exposed as span<uint8_t>; (3) name_table.hpp: (uint32 numeric_id, string name) pairs for forward-compat enum round-trip; (4) json_sidecar.hpp: write-only flat-object/array emitter, no third-party JSON dep; (5) unit tests: round-trip primitives, varint edges, truncated reads, bad magic, version-too-new, unknown-chunk-tag, name-table round-trip; (6) engine/asset/CLAUDE.md documents the seven Save Format Extensibility Rules + new primitives; (7) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #663
  - **Notes:** Foundation for .vxs, .rig, and world-snapshot formats (T-167, T-168, T-169, #667). Blocker #662 (doc rename) merged as PR #673. No functional consumer until T-167+. World-snapshot (#667) lives in engine/world/ and consumes these primitives; engine/asset/ must not depend on engine/entity/ or engine/world/.
  - **Links:**


- [~] **render: SDF→trixel half-voxel / lone-trixel discrepancy investigation** — reproduce, classify, and fix or document the artifact difference between C_VoxelSetNew voxel-pool output and direct-SDF SHAPES_TO_TRIXEL rasterization at silhouette boundaries
  - **ID:** T-190
  - **Area:** engine/render, shaders/glsl
  - **Model:** opus
  - **Owner:** claude/T-190-sdf-trixel-discrepancy
  - **Blocked by:** (none)
  - **Acceptance:** (1) diff report comparing voxel-pool vs SDF output at zoom 4/8/16 for box, sphere, cone, and torus shapes using tools/img_diff; (2) either a fix PR that eliminates the trixel discrepancy, or a CLAUDE.md note in engine/render/ documenting the intentional delta and its source (which threshold, which solver path); (3) fleet-build clean on linux-debug
  - **Issue:** #690
  - **Notes:** Human observation from PR #659 (T-163 stateless particle render): SDF path emits half-extent trixels or isolated single-trixel artifacts at silhouette boundaries that the voxel-pool path does not produce for the same shape. Investigate: (a) off-by-one from kSdfBiasEpsilon or stableCeilToInt ceiling bias at borderline depths; (b) 2x3 trixel diamond emit painting both subpixels when only one should fire near edge cases; (c) bug in snapLatticeWalk vs findSurfaceDepth. Focus: c_shapes_to_trixel.glsl (boxDepthIntersect/sphereDepthIntersect/snapLatticeWalk) vs c_voxel_to_trixel_stage_1.glsl (localIDToFace_2x3/faceOffset_2x3 emit). The snap mode (subdivisions==1) is designed to match C_VoxelSetNew trixel-for-trixel — divergence there is more likely a bug than intentional.
  - **Links:**

- [~] **voxel: refactor C_VoxelSetNew pool API — remove IRRender::allocateVoxels from component ctor (T-201 step 3)** — move or re-home the voxel pool allocator so C_VoxelSetNew no longer calls IRRender::allocateVoxels / deallocateVoxels directly
  - **ID:** T-206
  - **Area:** engine/render, engine/world, engine/prefabs/irreden/voxel, engine/script
  - **Model:** opus
  - **Owner:** opus-worker-2
  - **Blocked by:** (none)
  - **Acceptance:** (1) `component_voxel_set.hpp` no longer includes `<irreden/ir_render.hpp>` or `<irreden/render/texture.hpp>`; (2) `IRShapeDebug`, `voxel_editor`, and other demos using `C_VoxelSetNew` continue to render correctly; (3) no hot-path regression in voxel pool allocation (verify via visual smoke or existing benchmarks); (4) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #754
  - **Notes:** Step 3 of T-201 four-PR layering refactor. Step 1 landed PR #752; step 2 tracked by T-205 (#753). Architect must choose: (a) move pool allocator to engine/world/ (ergonomics preserved, pool changes owning module); (b) push allocation up to callers — C_VoxelSetNew ctor becomes data-only, a separate system claims pool space at first tick (generalizes existing `pendingVoxels_` staging path). Performance contract: ctor is hot for moving shapes — no virtual indirection, no per-call hash lookup. Two ctor sites plus dtor in component_voxel_set.hpp:81,170,229. Step 4 (T-207) is blocked on this landing.
  - **Links:**

- [ ] **script: re-remove IrredenEngineRendering from engine/script/CMakeLists.txt (T-201 step 4)** — final cleanup once T-205 + T-206 clear: drop the render link so IrredenEngineScripting has no dependency on IrredenEngineRendering
  - **ID:** T-207
  - **Area:** engine/script
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-206
  - **Acceptance:** (1) `engine/script/CMakeLists.txt` no longer links `IrredenEngineRendering`; (2) fresh-configure build from a clean build dir (`rm -rf build && cmake --preset linux-debug`) succeeds for `IrredenEngineScripting`, `IrredenEngineTest`, and `IRShapeDebug`; (3) all `PrefabApi.*` tests pass; (4) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #755
  - **Notes:** Closes #739 (T-201 as a whole) once this PR merges. T-189 (#729) re-added the render link as a temporary workaround; this PR removes it for good. Mandatory clean-configure build: PR #729 showed how easy it is to mask a build break with cached artifacts. Mechanical — just link-list edit + build validation.
  - **Links:**

- [~] **editor: F-1.1 — place/erase + palette panel + undo stack** — core authoring loop: left-click places, right-click erases, palette panel with ≥16 swatches, per-stroke undo stack with eviction cap
  - **ID:** T-211
  - **Area:** creations/editors, engine/prefabs/irreden/voxel
  - **Model:** opus
  - **Owner:** claude/T-211-place-erase-palette-undo
  - **Blocked by:** #603 (Phase 0 Foundation — requires #620 UI primitives, #621 per-voxel metadata, #628 voxel picking to be closed)
  - **Stack:** T-211..T-215 editor-phase-1
  - **Acceptance:** (1) left-click on voxel face places adjacent voxel in active layer with active palette color; right-click erases; (2) palette panel renders ≥16 swatches; clicking selects active color used for subsequent placements; (3) editing a swatch updates consistently (document chosen model: mutable vs. immutable-index); (4) Ctrl-Z reverses last stroke; Ctrl-Y re-applies; multi-step undo/redo works; (5) undo stack respects documented memory cap, oldest records evict; (6) no allocations inside per-voxel placement hot path (reserve at stroke begin); (7) fleet-build clean on linux-debug
  - **Issue:** #761
  - **Notes:** Undo data layout (delta vs. snapshot, eviction policy, palette-index vs. raw-RGBA storage) is the first undo system in the engine — choice constrains every later Phase 1/2/3 system. Part of entity-editor epic #604 / umbrella #213. See `docs/design/entity-editor-epic.md` §Phase 1.
  - **Links:**

- [~] **editor: F-1.2 — symmetry modes (X/Y/Z mirror, user-set plane offset)** — three independent mirror toggles; each axis has an adjustable mirror-plane offset; mirrored placements fold into the same undo record as the source
  - **ID:** T-212
  - **Area:** creations/editors
  - **Model:** sonnet
  - **Owner:** claude/T-212-symmetry-modes
  - **Blocked by:** T-211
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
  - **Blocked by:** T-211
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
  - **Blocked by:** T-211
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
  - **Blocked by:** T-211, T-213, T-214
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

- [~] **tooling: allow fleet agents to force-push claude/* branches in settings.json** — replace the broad `git push --force-with-lease` deny with branch-scoped denies that protect master/main but allow fleet rebases on `claude/*` branches
  - **ID:** T-218
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** claude/T-218-force-push-allow
  - **Blocked by:** (none)
  - **Acceptance:** (1) `claude/*` branch force-with-lease pushes succeed without a human approval prompt in headless fleet mode; (2) `git push --force-with-lease origin master` and `origin main` remain blocked; (3) `git push --force` to any branch remains blocked; (4) a fleet agent can successfully rebase and force-push a `claude/*` branch end-to-end (verify with T-199 PR #787 or a fresh test branch)
  - **Issue:** #783
  - **Notes:** Escalated from PR #756/PR #787 (T-199) — two opus-worker iterations independently completed the rebase correctly but both hit the same wall: `.claude/settings.json` line with `"Bash(git push --force-with-lease:*)"` in the deny list blocks all force-with-lease pushes. Option A (preferred): remove `Bash(git push --force-with-lease:*)`, add `Bash(git push --force-with-lease origin master:*)` and `Bash(git push --force-with-lease origin main:*)`. Option B: add targeted allow `Bash(git push --force-with-lease origin claude/*)` — only works if harness honors allow-before-deny specificity; verify before using. Keep `Bash(git push --force:*)` in deny list either way. This is a settings.json-only change.
  - **Links:**

- [~] **entity: dedup globalFieldRegistry — return stable FieldBindingId on repeated registerField calls** — add name→id reverse check in registerField so re-registering the same field name across World restarts returns the same id
  - **ID:** T-220
  - **Area:** engine/prefabs/irreden/common
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-1
  - **Blocked by:** (none)
  - **Acceptance:** (1) `registerField("foo")` called twice returns the same `FieldBindingId`; (2) test asserts id stability across two `World` constructions (or two fixture instances) within one process; (3) no regression in modifier_runtime_test / modifier_lua_test; (4) fleet-build clean on linux-debug
  - **Issue:** #512
  - **Notes:** Follow-up nit from PR #508 (T-100) Opus recheck. No reverse name→id check in `modifier_field_registry.hpp:25-29`. Lua side is guarded by `em.isComponentRegistered` at first registration, but across World teardown+reconstruct the same field name appends again with a fresh id — id drifts silently. Registry is small (<100 entries) so an O(N) linear scan per registration is fine (init-time only). Fix: scan `m_names` in `registerField` and return existing id if name already present.
  - **Links:**

- [~] **render: extend castVoxelRay to walk C_VoxelSetNew entities** — add CPU-side ray traversal over individual voxels in C_VoxelSetNew pool spans, returning hit world position, voxel coordinate, owning entity ID, and face normal; prerequisite for T-211 voxel editor picking
  - **ID:** T-219
  - **Area:** engine/prefabs/irreden/render, engine/world
  - **Model:** opus
  - **Owner:** claude/T-219-castvoxelray-voxel-sets
  - **Blocked by:** (none)
  - **Acceptance:** (1) `castVoxelRay` returns hits for active voxels of `C_VoxelSetNew` entities including world position, voxel coordinate, owning entity ID, and face normal; (2) face normal computed as `sign(largestAbsComponent(worldHitPos - voxelCenter))` — suitable for place-adjacent operations; (3) works for multiple `C_VoxelSetNew` entities in the same scene — smoke test with two voxel sets verifies correct hit on each; (4) existing `C_ShapeDescriptor` picking behavior unchanged; (5) `picking.hpp` doc comment documents CPU-side default vs GPU readback fallback (`IRRender::getEntityIdAtMouseTrixel`); (6) builds clean on linux-debug and macos-debug
  - **Issue:** #792
  - **Notes:** Prerequisite for T-211 (editor F-1.1 place/erase); PR #785 (T-211's first PR) is design-blocked on this and will rebase once it lands. CPU-side path has no frame lag — preferred for editor click responsiveness. GPU readback path (`IRRender::getEntityIdAtMouseTrixel`, 1-frame lag, O(1) per pick) is documented as the fallback for high-voxel-count scenes. Extend `gatherVisibleShapes` or add a sibling helper; treat each active voxel as an axis-aligned unit cube for SDF testing. Multi-sub-entity scenes, voxel-set joints, and growable pool allocation are explicitly out of scope.
  - **Links:**


- [ ] **docs: audit role-*.md — shared protocols + point-don't-dump** — read all 7 `.claude/commands/role-*.md` files, produce `docs/agents/audit-roles.md` enumerating verbatim/near-verbatim duplication blocks (≥5 lines), dump violations, stale content, and one-PR-each follow-up cleanup tasks
  - **ID:** T-221
  - **Area:** docs
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `docs/agents/audit-roles.md` present in merged PR; (2) note covers all 7 role files with file:line citations for each duplication; (3) each duplication names a right shared home (existing doc or proposed new doc); (4) follow-up GitHub issues filed (no labels) for each concrete cleanup task in the note
  - **Issue:** #800
  - **Notes:** Roles total ~3,500 lines; `role-opus-worker.md` is 1,242 lines alone. Likely duplication: commit/PR rules, gh invocations, fleet-cache structure, ECS baseline restatements, label state-machine descriptions. Candidate shared homes: `CLAUDE-BASELINE.md`, `FLEET.md`, `FLEET-CACHE.md`, `BUILD.md`, or new `docs/agents/REVIEWER-PROTOCOL.md`. Companion to skills/ audit and CLAUDE.md audit sibling research tasks. Issue author expects the findings note itself to feed the queue-manager with follow-up tasks via filed issues.
  - **Links:**

- [~] **render/picking: drop dead SDF box guard + unreachable flatIdx check; port 4 extra tests** — remove two provably-unreachable guards from `castVoxelRay` voxel-set branch and cherry-pick 4 face-normal tests from closed PR #796
  - **ID:** T-224
  - **Area:** engine/prefabs/irreden/render, test/render
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-1
  - **Blocked by:** (none)
  - **Acceptance:** (1) `picking.hpp` voxel-set branch no longer contains the SDF box guard (`SDF::box(delta, vec3(0.5)) > 0`) or the defensive `flatIdx >= voxels_.size()` check; (2) `picking_face_normal_test.cpp` grows from 6 to 10 `TEST(` entries (NegativeVoxelCoordPlusXFace, OriginVoxelMinusZFace, ExactCenterFavorsXAxisPositive, ReturnsSingleNonzeroAxis); (3) `fleet-build --target IrredenEngineTest` clean on linux-debug and macos-debug; (4) `fleet-run IrredenEngineTest` — all PickingFaceNormal tests pass; (5) `fleet-build --target IRVoxelEditor` clean; (6) no behavioral change in IRShapeDebug or IRVoxelEditor voxel picking (smoke)
  - **Issue:** #807
  - **Notes:** Follow-up from T-219 / PR #795 vs #796 duplicate disposition. PR #796 was closed as duplicate (PR #795 landed as canonical T-219); #796 contained two real dead-code simplifications + four extra tests worth cherry-picking. Expected diff: ~6 lines removed in picking.hpp, ~50 lines added in test file. Cherry-pick test bodies verbatim from PR #796's diff, adapting names for consistency with existing 6 tests.
  - **Links:**

- [~] **docs: audit SKILL.md files — shared protocols + point-don't-dump** — read all 16 `.claude/skills/*/SKILL.md` files, produce `docs/agents/audit-skills.md` enumerating duplicated ≥5-line blocks, ECS/naming/IRMath restatements, drift between composing skills, slop, and one-PR-each follow-up cleanup tasks
  - **ID:** T-222
  - **Area:** docs
  - **Model:** opus
  - **Owner:** opus-worker-1
  - **Blocked by:** (none)
  - **Acceptance:** (1) `docs/agents/audit-skills.md` present in merged PR; (2) note covers all 16 SKILL.md files with file:line citations for each duplicated block; (3) each duplication names a right shared home (existing baseline doc or proposed new doc); (4) drift between composing skills identified (e.g. commit-and-push vs polish-checkpoint vs simplify); (5) slop flagged: redundant intros, dead examples referencing retired components or symbols; (6) concrete follow-up cleanup tasks enumerated, each sized for one PR; (7) follow-up GitHub issues filed (no labels) for each cleanup proposal
  - **Issue:** #801
  - **Notes:** 16 SKILL.md files totaling ~5,400 lines; simplify (645), review-pr (523), commit-and-push (474), lua-creation-setup (470) are the biggest. Several skills compose: commit-and-push invokes simplify; polish-checkpoint mirrors commit-and-push's pre-commit phase; render-debug-loop uses render-verify. Companion to T-221 (roles audit). Deliverable is a research findings note — no code changes.
  - **Links:**

- [~] **docs: audit CLAUDE.md files — baseline drift, dead pointers, slop** — read all CLAUDE.md files across engine and creations; produce `docs/agents/audit-claude-md.md` listing per-file duplications, dead symbol refs, rule contradictions, slop, and concrete follow-up cleanup tasks
  - **ID:** T-223
  - **Area:** docs
  - **Model:** opus
  - **Owner:** opus-worker-2
  - **Blocked by:** (none)
  - **Acceptance:** (1) `docs/agents/audit-claude-md.md` present in merged PR; (2) per-file findings covering all CLAUDE.md files with categories: baseline duplications, dead symbol/type/path pointers (verified with grep), rule contradictions, slop, and missing-but-should-document candidates; (3) concrete follow-up GitHub issues filed (no labels) grouped by subtree (e.g. `engine/render/`, `engine/prefabs/`, `creations/` as separate PRs); (4) fleet-build clean on linux-debug
  - **Issue:** #802
  - **Notes:** Companion to T-221 (#800, role-*.md audit) and T-222 (#801, skills audit). Known stale refs post-transform-migration: `C_Position3D`, `C_PositionOffset3D`, `C_PositionGlobal3D`, `C_Rotation` still referenced in some module CLAUDE.md files. 31 CLAUDE.md files across the repo — pattern is root + per-module inheriting from `docs/agents/CLAUDE-BASELINE.md`. Suggested cleanup PR grouping from issue: one PR per subtree.
  - **Links:**

- [~] **docs/skills: fix cross-repo info-isolation leaks in midi-scene-creator and create-creation** — remove game-repo references from two SKILL.md files that name the private game repo, violating CLAUDE-BASELINE §Cross-repo info isolation
  - **ID:** T-225
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-2
  - **Blocked by:** (none)
  - **Acceptance:** (1) `.claude/skills/midi-scene-creator/SKILL.md:164-169` and `.claude/skills/create-creation/SKILL.md:186` no longer reference the game repo by name or role; (2) engine-public alternatives (e.g., `creations/demos/default/`) used instead or section removed; (3) no other `irreden` / game repo references remain in either file
  - **Issue:** #808
  - **Notes:** Follow-up from T-222 audit (§5.5, §2 row 9, §4.3). XS mechanical fix. CLAUDE-BASELINE.md:228-292 is the canonical isolation rule.
  - **Links:**

- [~] **docs/skills: add missing 'name:' field to request-re-review/SKILL.md front-matter** — add `name: request-re-review` to the only SKILL.md missing that front-matter field
  - **ID:** T-226
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-1
  - **Blocked by:** (none)
  - **Acceptance:** (1) `.claude/skills/request-re-review/SKILL.md` front-matter contains `name: request-re-review` above `description:`; (2) all 16 SKILL.md files now carry both `name:` and `description:` fields
  - **Issue:** #809
  - **Notes:** Follow-up from T-222 audit (§5.20, §4.3). XS — one-line insertion. 15/16 other skills already have both fields.
  - **Links:**

- [~] **docs/skills: unify --auto-screenshot contract symbol names across render-debug-loop and render-verify** — resolve stale-vs-current symbol name divergence and move canonical description to engine/video/CLAUDE.md
  - **ID:** T-227
  - **Area:** docs, engine/video
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-2
  - **Blocked by:** (none)
  - **Acceptance:** (1) verify against `engine/video/` which symbol set is current (`IRVideo::parseAutoScreenshotArgv` + `IRVideo::AutoScreenshotShot` + `IRVideo::createAutoScreenshotSystem` vs `ShotConfig` / `g_shots[]` / `AutoScreenshot` system); (2) canonical contract description added to `engine/video/CLAUDE.md`; (3) both `render-debug-loop/SKILL.md` and `render-verify/SKILL.md` reduced to one line + link
  - **Issue:** #810
  - **Notes:** Follow-up from T-222 audit (§5.9, §3.1). Size M. One of the two symbol sets is stale — likely render-verify's pre-extraction form.
  - **Links:**

- [ ] **docs/skills: align model-version stamps emitted by review-pr and commit-and-push** — fix stale Opus 4.6 stamp in review-pr and align co-author stamp with harness
  - **ID:** T-228
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `review-pr/SKILL.md:393` stale `Opus 4.6` stamp updated or removed; (2) `commit-and-push/SKILL.md:199` co-author stamp consistent with harness system prompt; (3) either both skills drop the model-version stamp (letting the harness add it) or both use one canonical signature constant documented in a shared doc
  - **Issue:** #811
  - **Notes:** Follow-up from T-222 audit (§5.10, §3.4). Size S. Three artifacts with three different stamps; canonical Opus is now 4.7.
  - **Links:**

- [ ] **docs/skills: pick a single formatter owner across simplify / commit-and-push / polish-checkpoint** — eliminate the three-way disagreement on who runs clang-format
  - **ID:** T-229
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) formatter step (fleet-build --target format-changed or equivalent) owned by exactly one skill; (2) the other two skills do not mention a separate format step; (3) decision documented with a one-sentence rationale in the owning skill
  - **Issue:** #812
  - **Notes:** Follow-up from T-222 audit (§5.11, §3.3). Size S. Three-way conflict: simplify runs `fleet-build --target format-changed`; polish-checkpoint runs `fleet-build --target format` after simplify; commit-and-push invokes simplify but omits format.
  - **Links:**

- [ ] **docs/skills: reconcile backend-parity's proactive start-next-task chaining with start-next-task's no-auto-invoke contract** — remove the contradictory chaining instruction from one of the two skills
  - **ID:** T-230
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) contradiction resolved — either `backend-parity/SKILL.md:331-335` drops the "chain to start-next-task" instruction, or `start-next-task/SKILL.md:46-70` documents a backend-parity exemption; (2) no other skill issues an auto-invoke of start-next-task without citing the exemption
  - **Issue:** #813
  - **Notes:** Follow-up from T-222 audit (§5.12, §3.2). XS. backend-parity tells agents to chain to start-next-task proactively; start-next-task says "Do not invoke proactively."
  - **Links:**

- [ ] **docs/skills: extract ECS-invariants checklist into .claude/rules/cpp-ecs-smells.md** — consolidate the per-entity getComponent / structural-change / allocation-in-tick check from 5 SKILL.md files into one shared rules file
  - **ID:** T-231
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `.claude/rules/cpp-ecs-smells.md` created with the canonical ECS-invariants checklist; (2) `simplify/SKILL.md:62-105`, `review-pr/SKILL.md:200-222`, `optimize/SKILL.md:194-200`, `polish-checkpoint/SKILL.md:88-94`, `ecs-prefab-creator/SKILL.md:131-133` each replaced with a one-line ref to the new file; (3) `.claude/rules/cpp-systems.md` ref pattern at `simplify/SKILL.md:119-122` used as the ref style
  - **Issue:** #814
  - **Notes:** Follow-up from T-222 audit (§5.1, §1.1, §2). Size M. Same checklist restated in 5 files; canonical source is CLAUDE-BASELINE.md:16-31 + engine/system/CLAUDE.md.
  - **Links:**

- [ ] **docs/skills: move IRMath substitution table into .claude/rules/cpp-math.md** — consolidate the IRMath→glm substitution table from simplify and 3 other skills into one shared rules file
  - **ID:** T-232
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) IRMath substitution table moved from `simplify/SKILL.md:144-154` into `.claude/rules/cpp-math.md` (already referenced at `simplify/SKILL.md:119`); (2) `backend-parity/SKILL.md:218-222`, `optimize/SKILL.md:211-213`, `review-pr/SKILL.md:268-273` each replaced with a one-line ref; (3) canonical source remains `CLAUDE-BASELINE.md:92-107`
  - **Issue:** #815
  - **Notes:** Follow-up from T-222 audit (§5.2, §1.3). Size S. `lua-creation-setup/SKILL.md:294` already uses the correct one-line ref form.
  - **Links:**

- [ ] **docs/skills: replace naming-table copies with one-line refs to CLAUDE-BASELINE** — remove duplicated naming-convention tables from 5 SKILL.md files
  - **ID:** T-233
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `simplify/SKILL.md:308-321`, `ecs-prefab-creator/SKILL.md:22-23` and `200-206`, `render-trixel-pipeline/SKILL.md:106-114`, `backend-parity/SKILL.md:121-132`, `review-pr/SKILL.md:283-289` each use a one-sentence ref to `CLAUDE-BASELINE.md §Naming` (and keep only regex-style fix patterns they specifically use); (2) duplicate tables removed; (3) canonical home `CLAUDE-BASELINE.md:36-47` unchanged
  - **Issue:** #816
  - **Notes:** Follow-up from T-222 audit (§5.3, §1.2). Size S. Five files restate the same table.
  - **Links:**

- [ ] **docs/skills: shrink commit-and-push cross-repo info-isolation procedure to a check + baseline ref** — replace 38-line inline procedure with a ~2-line check + baseline pointer
  - **ID:** T-234
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `commit-and-push/SKILL.md:118-156` replaced with ~2 lines: scan diff for tokens listed in `CLAUDE-BASELINE.md §Cross-repo information isolation`; warn if matched; (2) anti-pattern restatement at `commit-and-push/SKILL.md:462-464` removed; (3) no cross-repo policy content duplicated from baseline
  - **Issue:** #817
  - **Notes:** Follow-up from T-222 audit (§5.4, §1.4). Size S. `CLAUDE-BASELINE.md:273` says "commit-and-push checks for this" but the skill restates the whole policy (~38 lines).
  - **Links:**

- [ ] **docs/skills: consolidate fleet-build/fleet-run snippets into one canonical block in BUILD.md** — fix five different timeout variants across 6 skills and eliminate a raw cmake --build violation in backend-parity
  - **ID:** T-235
  - **Area:** docs, build
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) canonical fleet-build/fleet-run wrapper + timeout guidance block added to `docs/agents/BUILD.md`; (2) `simplify/SKILL.md:573-576`, `polish-checkpoint/SKILL.md:107,122`, `attach-screenshots/SKILL.md:183-185,230-231`, `optimize/SKILL.md:120-122`, `render-debug-loop/SKILL.md:66,89`, `render-verify/SKILL.md:102-104` each reference BUILD.md and state only their per-skill timeout choice; (3) `backend-parity/SKILL.md:253-269` raw `cmake --build build --target IRShapeDebug -j$(nproc)` replaced with `fleet-build`
  - **Issue:** #818
  - **Notes:** Follow-up from T-222 audit (§5.6, §1.6). Size M. backend-parity's raw cmake trips the Bash-tool command_substitution gate (CLAUDE-BASELINE.md:180-219).
  - **Links:**

- [ ] **docs/skills: replace host/preset table copies with refs to BUILD.md** — remove duplicate host/preset tables from render-verify, render-debug-loop, backend-parity
  - **ID:** T-236
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `render-verify/SKILL.md:42-46`, `render-debug-loop/SKILL.md:26-29`, `backend-parity/SKILL.md:253-269` (variant) each replaced with a two-line ref to `docs/agents/BUILD.md`; (2) no duplicate host/preset table remains in these three files
  - **Issue:** #819
  - **Notes:** Follow-up from T-222 audit (§5.7, §1.7). XS. Canonical home is docs/agents/BUILD.md (already referenced from engine-root CLAUDE.md).
  - **Links:**

- [ ] **docs/skills: shrink start-next-task's cursor-stack-base coverage; defer mechanism to FLEET.md** — cut ~40 lines from start-next-task/SKILL.md by deferring the cursor-stack-base mechanism description to FLEET.md
  - **ID:** T-237
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) `start-next-task/SKILL.md` sections `34-39`, `159-186`, `270-291` trimmed to flow descriptions only; mechanism deferred to `FLEET.md:140-186` ref; (2) net reduction ~40 lines; (3) `commit-and-push/procedures/cursor-stack.md` still correctly scoped to PR-creation deltas only
  - **Issue:** #820
  - **Notes:** Follow-up from T-222 audit (§5.8, §1.5). Size S. Canonical mechanism already in FLEET.md:140-186.
  - **Links:**

- [ ] **docs/skills: lift commit-and-push PR-body HEREDOC templates into procedures/pr-body.md** — extract three near-identical PR-body templates into one canonical template with per-mode delta sections
  - **ID:** T-238
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) new `.claude/skills/commit-and-push/procedures/pr-body.md` with one canonical PR-body template + single-PR / fleet-stacked / cursor-stacked delta sections; (2) `commit-and-push/SKILL.md:264-374` three templates (~110 lines) replaced with ~2-line refs; (3) net reduction ~50 lines from main SKILL.md
  - **Issue:** #821
  - **Notes:** Follow-up from T-222 audit (§5.13, §1.9). Size M. Three templates differ by only 1-2 fields. Existing `procedures/` pattern: fleet-stack, cursor-stack, rebase-guard.
  - **Links:**

- [ ] **docs/skills: lift commit-and-push host-stamp logic into procedures/host-label.md** — extract 37-line host-stamp shell logic into a shared procedures file
  - **ID:** T-239
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) new `.claude/skills/commit-and-push/procedures/host-label.md` with the `fleet:authored-on-{linux,macos}` host-stamp logic; (2) `commit-and-push/SKILL.md:386-423` (~37 lines) replaced with a ref; (3) reviewer-side `review-pr/SKILL.md:478-484` already refs `review-pr/procedures/cross-host-smoke.md` — ensure consistency
  - **Issue:** #822
  - **Notes:** Follow-up from T-222 audit (§5.14, §1.11). Size S. Existing procedures/ pattern: cross-host-smoke.md, cursor-stack.md, rebase-guard.md.
  - **Links:**

- [ ] **docs/skills: lift simplify's 98-line serialization version-bump rule into engine/asset/CLAUDE.md** — move asset-format-specific version-bump detection policy out of the generalist simplify skill
  - **ID:** T-240
  - **Area:** docs, engine/asset
  - **Model:** sonnet
  - **Owner:** free
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

- [x] **T-209** — modifier: replace ticksRemaining footgun with pushFrameLocal / pushOneFrame · Owner: claude/T-209-pushFrameLocal-rebased · PR: https://github.com/jakildev/IrredenEngine/pull/784
- [x] **T-203** — Linux demo validation suite — fix SHAPES_TO_TRIXEL 2D dispatch crash + all demos pass on linux-debug · Owner: claude/T-203-linux-demo-validation · PR: https://github.com/jakildev/IrredenEngine/pull/782
- [x] **T-216** — tooling: add Bash(gh:*) to fleet baseline + create fleet-iteration-summary · Owner: claude/T-216-ubuntu-approved-label-fix · PR: https://github.com/jakildev/IrredenEngine/pull/780
- [x] **T-210** — generalize APPLY_POSITION_OFFSET into applyVec3ModifierTo<> · Owner: claude/T-210-apply-vec3-modifier-to · PR: https://github.com/jakildev/IrredenEngine/pull/779
- [x] **T-208** — modifier: writer-owned slot API — upsertBySource to eliminate per-frame push_back churn · Owner: claude/T-208-modifier-upsert-by-source · PR: https://github.com/jakildev/IrredenEngine/pull/776
- [x] **T-202** — enable Linux/OpenGL backend on WSLg (GL 4.5 + GLSL hygiene) · Owner: claude/T-202-linux-opengl-parity · PR: https://github.com/jakildev/IrredenEngine/pull/775
- [x] **T-205** — move getActiveCanvasEntityOrNull out of ir_render.hpp · Owner: claude/T-205-active-canvas-decouple · PR: https://github.com/jakildev/IrredenEngine/pull/772
- [x] **T-204** — entity: fix sortArchetypeNodesByRelationChildOf — BFS seeds leaves instead of roots · Owner: claude/T-204-sort-archetype-bfs-fix · PR: https://github.com/jakildev/IrredenEngine/pull/770
- [x] **T-200** — joints as entities — C_Skeleton + C_Joint scaffolding (replace SoA C_JointHierarchy) · Owner: claude/T-200-skeleton-joint-entities · PR: https://github.com/jakildev/IrredenEngine/pull/751
- [x] **T-197** — C_LocalTransform (SQT) + C_WorldTransform + SYSTEM_PROPAGATE_TRANSFORM · Owner: claude/T-197-sqt-transform-propagate · PR: https://github.com/jakildev/IrredenEngine/pull/749
- [x] **T-198** — quat modifier kind — extend modifier compose for rotation perturbations · Owner: claude/T-198-quat-modifier-kind · PR: https://github.com/jakildev/IrredenEngine/pull/748
- [x] **T-193** — Lua input & command bindings (PR 2/2 implementation) · Owner: claude/T-193-lua-input-commands-impl · PR: https://github.com/jakildev/IrredenEngine/pull/747
- [x] **T-192** — delete C_PositionOffset3D — migrate idle bob + gizmo to vec3 modifiers · Owner: claude/T-192-delete-position-offset · PR: https://github.com/jakildev/IrredenEngine/pull/746
- [x] **T-196** — Research — Lua binding automation (codegen extension + shared default bindings header) · Owner: claude/T-196-lua-binding-codegen-research · PR: https://github.com/jakildev/IrredenEngine/pull/745
- [x] **T-194** — Research: Lua physics bindings — enumerate physics surface and propose Lua API · Owner: claude/T-194-lua-physics-research · PR: https://github.com/jakildev/IrredenEngine/pull/744
- [x] **T-195** — docs: update lua-creation-setup skill for codegen + Lua-defined components/systems · Owner: claude/T-195-lua-creation-setup-docs · PR: https://github.com/jakildev/IrredenEngine/pull/742
- [x] **T-191** — vec3 modifier kind — extend modifier compose for vector fields · Owner: claude/T-191-vec3-modifier-kind · PR: https://github.com/jakildev/IrredenEngine/pull/740
- [x] **T-189** — prefab attach DENSE/HYBRID voxel_ref as C_VoxelSetNew on spawn · Owner: claude/T-189-prefab-dense-attach · PR: https://github.com/jakildev/IrredenEngine/pull/729
- [x] **T-187** — render LOD Phase 1 — computeLodLevel + per-shape lodMin filter · Owner: claude/T-187-lod-phase-1 · PR: https://github.com/jakildev/IrredenEngine/pull/727
- [x] **T-186** — test: JsonSidecarWriter + NameTable round-trips · Owner: claude/T-186-json-sidecar-name-table-tests · PR: https://github.com/jakildev/IrredenEngine/pull/730
