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

- [ ] **voxel: refactor C_VoxelSetNew pool API — remove IRRender::allocateVoxels from component ctor (T-201 step 3)** — move or re-home the voxel pool allocator so C_VoxelSetNew no longer calls IRRender::allocateVoxels / deallocateVoxels directly
  - **ID:** T-206
  - **Area:** engine/render, engine/world, engine/prefabs/irreden/voxel, engine/script
  - **Model:** opus
  - **Owner:** free
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
