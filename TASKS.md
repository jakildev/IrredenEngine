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

- [ ] **Lua-driven ECS: Lua port of perf_grid + perf parity gate** — new demo creations/demos/lua_perf_grid/ mirroring perf_grid (262k entities, wave animation, same render pipeline) entirely in Lua; parity gate: Lua wave-animation per-tick cost <= 1.5x C++ equivalent
  - **ID:** T-104
  - **Area:** engine/script, creations/demos/lua_perf_grid
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) fleet-build --target IRLuaPerfGrid clean on linux-debug; (2) fleet-run IRLuaPerfGrid runs without crash (64x64x64 voxel grid, wave animation, same render pipeline as perf_grid); (3) parity gate: Lua wave-animation system per-tick cost <= 1.5x C++ SystemPeriodicIdlePositionOffset per-tick cost measured via IRProfile with profiling_enabled=true; (4) measured ratio documented in docs/design/lua-driven-ecs.md retrospective; (5) if gate fails: design doc PR amended with corrective decision before further work
  - **Issue:** #492
  - **Notes:** PR 6 of 6 for parent epic #293 — formal acceptance gate for the entire Lua-driven ECS stack. Full architect plan in .fleet/plans/T-104.md. Blocked by T-103 (hot-reload). If parity gate fails, this PR does not merge; instead amend T-099's design doc with corrective decision (LuaJIT migration, codegen-bound bodies, etc.).
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



- [~] **Editor F-0.7: JSON sidecar format for .txl** — .txl.json alongside every .txl save; stores bind-points, component-pack fields, material-registry references; silent when absent
  - **ID:** T-151
  - **Area:** engine/prefabs/irreden/voxel, creations/editors
  - **Model:** sonnet
  - **Owner:** claude/T-151-txl-json-sidecar
  - **Blocked by:** (none)
  - **Acceptance:** (1) editor writes .txl.json next to .txl save (omit file if content empty); (2) loader reads sidecar if present; missing sidecar = empty bind-point list, empty component-pack, identity material map, no log; (3) round-trip test: write voxel grid + bind-points → reload → bind-points match exactly; (4) schema documented in same doc as .txl v2 spec from F-0.6
  - **Issue:** #626
  - **Notes:** Phase 0 (F-0.7) of entity-editor epic. Parent epic: #603. Umbrella: #213. Blocked by T-144 (epic doc) and T-146 (F-0.6 per-voxel metadata). Bind-point name vocabulary in game-side editor-needs.md (irreden#60). Engine stores component-pack values as untyped JSON; game side reads via its own registry.
  - **Links:**


- [~] **Editor F-0.9: voxel mouse picking (ray cast → world-space voxel selection)** — cursor-to-ray, DDA voxel grid intersection, single-voxel selection state with visual highlight
  - **ID:** T-153
  - **Area:** engine/render, creations/editors
  - **Model:** opus
  - **Owner:** claude/T-153-voxel-picking
  - **Blocked by:** (none)
  - **Acceptance:** (1) left-click on voxel in 3D viewport → selected and visually highlighted; (2) left-click on empty space → selection clears; (3) selection survives camera orbit/pan/zoom (only highlight redraws); (4) picking respects camera projection (orthographic + perspective); (5) selected voxel world-space position queryable via editor selection state for Phase 1+ tools
  - **Issue:** #628
  - **Notes:** Phase 0 (F-0.9) of entity-editor epic. Parent epic: #603. Umbrella: #213. Blocked by T-144 (epic doc), T-147 (F-0.8 editor scaffold), T-150 (F-0.4 camera). Selection-state design must anticipate Phase 1+ multi-selection. Phase 0 acceptance criterion in #603 explicitly requires mouse picking.
  - **Links:**


- [~] **Stateless procedural particle system — UBO-driven emitters** — Phase 1: types, C_StatelessParticleEmitters component, one compute render pass, GLSL + Metal parity, demo (64 emitters × 64 particles, gravity-with-jitter)
  - **ID:** T-163
  - **Area:** engine/render, engine/prefabs/irreden/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** claude/T-163-stateless-particles
  - **Blocked by:** (none)
  - **Acceptance:** (1) GpuParticleEmitter and FrameDataStatelessParticles types in ir_render_types.hpp (std140-friendly, 80 B per emitter); (2) C_StatelessParticleEmitters component (per-canvas std::vector<GpuParticleEmitter> + UBO) defined in engine/prefabs; (3) compute render pass GLSL shader with closed-form gravity-with-jitter trajectory, deterministic per (emitterId, subIndex, cycle) via pcg3d/hash3; (4) Metal parity using same scratch-buffer workaround as T-139 render kernel; (5) demo: 64 emitters × 64 particles drifting under gravity with color and position jitter; (6) fleet-build clean on linux-debug and macos-debug; (7) composites correctly on same canvas as T-139 SSBO particles via imageAtomicMin
  - **Issue:** #647
  - **Notes:** Alternative/complement to T-139 SSBO pool (#209, PR #614) — stateless path for ambient/decorative emitters (biome motes, rain, weather) with zero per-particle state and near-zero CPU→GPU sync cost. Phase 1 only; Phases 2–5 (Lua bindings, combined demo, benchmark, color ramps) filed as follow-ups after Phase 1 lands. ECS option (a) locked in by architect comment. UBO slot: pick unused 0–30, do not alias T-139's slot 23. Reuse iso-projection helpers from ir_iso_common.glsl and canvas binding pattern from system_render_gpu_particles_to_trixel.hpp (PR #614).
  - **Links:**


- [~] **Editor F-0.5 Phase 2: screen-space gizmo sizing + depth-aware dimming** — constant pixel-size gizmo handles across zoom range; depth-aware alpha dimming for occluded gizmos
  - **ID:** T-164
  - **Area:** engine/prefabs/irreden/render, engine/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** claude/T-164-gizmo-screen-space
  - **Blocked by:** (none)
  - **Acceptance:** (1) gizmos render at constant pixel size across full zoom range; (2) gizmo fragments behind world geometry show with reduced alpha (faint silhouette when occluded); (3) GLSL + Metal backends agree on dimming behavior; (4) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #675
  - **Notes:** Phase 2 follow-up to T-152 (Phase 1 shipped via PR #672). New UPDATE system scales C_ShapeDescriptor params_ inversely to camera zoom (member pixelSize_ on System<N>). New SHAPE_FLAG_GIZMO bit + depth-aware alpha path in c_shapes_to_trixel.glsl + shapes_to_trixel.metal. Touch points: system_gizmo_screen_space_size.hpp, shape_descriptor_flags.hpp, both shaders, gizmo.hpp, editor main.cpp, ir_system_types.hpp.
  - **Links:**


- [~] **Editor F-0.5 Phase 3: gizmo hover + drag interaction** — hover highlight via entity-id texture readback; drag-axis-constrained translate/scale; Shift-snap rotate
  - **ID:** T-165
  - **Area:** engine/prefabs/irreden/render, creations/editors
  - **Model:** opus
  - **Owner:** claude/T-165-gizmo-hover-drag
  - **Blocked by:** T-153
  - **Acceptance:** (1) hovering any handle highlights it visibly; leaving clears; (2) click-drag X arrow translates anchor entity only in X (similarly Y/Z); (3) drag rotate ring rotates anchor around that axis; Shift held snaps to 15° increments; (4) drag scale center uniformly scales; drag scale stick scales single axis only
  - **Issue:** #676
  - **Notes:** Phase 3 follow-up to T-152 (Phase 1 shipped via PR #672). Hover reuses entity-id texture readback built by T-153. New systems: system_gizmo_hover.hpp (reads canvas entity-id texture for cursor pixel, sets C_GizmoHandle::hover_), system_gizmo_drag.hpp (drag state per handle kind). Touch points: component_gizmo_handle.hpp, editor main.cpp, ir_system_types.hpp.
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


- [ ] **asset: .vxs v1 dense-mode reader/writer (BNDS, VOXR, LAYR, FRAM, META chunks)** — first end-to-end asset format: 3D bounded voxel volume with 12-byte per-voxel records, layers, and animation frame poses
  - **ID:** T-167
  - **Area:** engine/asset
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-166
  - **Acceptance:** (1) IRAsset::saveVoxelSet/loadVoxelSet ship; (2) .vxs v1 dense format: IRVS magic + uint32 version + chunk-count; MODE=DENSE; BNDS (ivec3 min/max); VOXR (12-byte records: Color + material_id + flags + bone_id + pad); LAYR (named layer bitmasks); FRAM (frame-index → per-voxel offset); META (free-form key/value); (3) unknown chunk tags silently skipped; (4) clear diagnostics on bad magic, version-too-new, truncated chunk, malformed BNDS; (5) round-trip unit test: 20³ fixture save → load → byte-compare VOXR + BNDS; (6) edge cases: empty set, full set, bad magic, truncated mid-VOXR; (7) chunk table documented in .hpp header block; (8) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #664
  - **Notes:** First consumer of T-166 binary-I/O primitives. Dense-mode only; shape-group mode is T-168; hybrid + sidecar is #668. C_Voxel layout (12 B) must match T-146 per-voxel widening — coordinate landing order matters. Replacement for txl v2 intent in PR #635 / #621.
  - **Links:**


- [~] **asset: .vxs v1 shape-group mode (SHPG, SREF chunks) — SDF primitive composition save format** — persist C_ShapeDescriptor instances as SDF primitive composition; new ShapeType values never break existing saves
  - **ID:** T-168
  - **Area:** engine/asset, engine/prefabs/irreden/voxel
  - **Model:** opus
  - **Owner:** claude/T-168-vxs-shape-group
  - **Blocked by:** T-166
  - **Acceptance:** (1) SHPG chunk: per-primitive record {uint32 shapeTypeId, uint16 version, vec4 params, Color, uint32 flags, uint8 bone_id, vec3 offset, quat rotation, uint8 csgOp}; csgOp ∈ {UNION, SMOOTH_UNION, SUBTRACT, INTERSECT, NONE}; (2) SREF chunk: string name-table for ShapeType enum + material registry refs; (3) MODE chunk gains SHAPES tag; (4) IRAsset::saveVoxelSet overload accepts span<const C_ShapeDescriptor> + optional per-primitive csgOp/transform; (5) loader returns list of C_ShapeDescriptor-shaped records for entity spawning; (6) round-trip test: 5-primitive shape group (sphere/box/capsule mix) save → load → byte-compare; (7) forward-compat test: unknown ShapeType numeric id → logs "unknown shape ID=N name=…, skipped", rest loads; (8) "How to add a new SDF primitive" walkthrough in engine/asset/CLAUDE.md; (9) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #665
  - **Notes:** Can run in parallel with T-167 after T-166 lands. Persists inputs to SHAPES_TO_TRIXEL pipeline — renders directly via that system, no voxelization needed. CSG tree can land in SHPT chunk later without bumping v1. Hybrid mode (#668) combines SHPG + VOXR.
  - **Links:**


- [~] **asset: .rig v1 — joints (JNTS) chunk; persist C_JointHierarchy** — on-disk format for joint hierarchies; rigs are a separate asset so the same skeleton can be shared across voxel variants
  - **ID:** T-169
  - **Area:** engine/asset, engine/prefabs/irreden/voxel
  - **Model:** opus
  - **Owner:** claude/T-169-rig-v1
  - **Blocked by:** T-166
  - **Acceptance:** (1) IRAsset::saveRig/loadRig ship; (2) .rig v1 format: IRRG magic + uint32 version + chunk-count; JNTS chunk: array of {uint16 version, vec4 rotation, vec4 translation, uint32 parentIndex, string name}; (3) loader produces C_JointHierarchy ready for toGPUFormat(); (4) round-trip 30-bone snake rig: toGPUFormat() produces identical GPU matrices pre/post round-trip; (5) JSON sidecar emits per-joint name + parent index; (6) unknown chunks handled forward-compat (Extensibility Rule #1); (7) unit tests: round-trip, unknown-chunk forward compat; (8) fleet-build clean on linux-debug
  - **Issue:** #666
  - **Notes:** Joints-only first slice; bind-points (#669) adds BIND chunk; Phase 3 animation tracks (#606) adds ANIM chunk — both without bumping .rig v1. Referenced by .prefab.lua files (#671) for rig sharing. Parent epic: #605 (Phase 2 — Hierarchies & skeletal voxels).
  - **Links:**


- [ ] **asset: .vxs hybrid mode + sidecar emitter + full test suite** — single .vxs carries both VOXR (dense) and SHPG (shape) chunks; .vxs.json sidecar on every save; comprehensive corrupt/truncated/version/unknown-tag test matrix
  - **ID:** T-170
  - **Area:** engine/asset
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-167, T-168
  - **Acceptance:** (1) MODE chunk gains HYBRID tag; (2) IRAsset::saveVoxelSet accepts both dense records + shape descriptors in one call; (3) loader returns struct with both populated; (4) .vxs.json sidecar emitted via json_sidecar.hpp (version, mode, bounds, material_registry_refs, layer_names, frame_count, shape_primitives_summary); (5) test suite covers dense round-trip, shape-group round-trip, hybrid (5 shapes + 50 dense voxels) round-trip, corrupt-magic, truncated mid-VOXR, version-too-new, unknown chunk tag, unknown ShapeType id; (6) fleet-build clean on linux-debug
  - **Issue:** #668
  - **Notes:** Synthesis ticket combining T-167 (dense) + T-168 (shape-group) into hybrid mode. Extends voxel_set_format.cpp; test suite in test_voxel_set_format.cpp. Parent epic: #604. Blocks T-173 (prefab Lua format).
  - **Links:**


- [~] **asset: .rig v2 — bind-points (BIND) chunk; persist C_BindPoints** — additive BIND chunk in .rig format persisting per-joint named anchor transforms; no version bump to .rig v1
  - **ID:** T-171
  - **Area:** engine/asset, engine/prefabs/irreden/voxel
  - **Model:** sonnet
  - **Owner:** claude/T-171-rig-v2-bind-chunk
  - **Blocked by:** T-169
  - **Acceptance:** (1) BIND chunk: array of {string name, uint32 bone_id, vec3 offset, vec4 rotation}; O(string-hash) lookup at load time; (2) IRAsset::saveRig/loadRig round-trip bind points alongside joints; (3) JSON sidecar lists bind-point names + bone ids; (4) round-trip test: 5 bind points on 30-bone snake rig, entity:bindPoint() returns identical world-space transforms pre/post; (5) forward compat: .rig without BIND loads with empty bind-point map; (6) forward compat: old build skips unknown BIND chunk (Extensibility Rule #1); (7) fleet-build clean on linux-debug
  - **Issue:** #669
  - **Notes:** Phase 5 of editor epic (#608). Purely additive to .rig v1 — no version bump. component_bind_points.hpp may be defined here or in #608 depending on phase order. Blocks T-173 (prefab Lua format).
  - **Links:**


- [~] **tooling: simplify + review-pr serialized-struct version-bump check** — pre-commit and PR-review check that flags serialized struct field changes not accompanied by a kSaveVersion bump
  - **ID:** T-172
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** claude/T-172-serialized-struct-version-check
  - **Blocked by:** #667
  - **Acceptance:** (1) simplify check grep-walks diff for changes inside serialized-tagged structs and emits a finding if kSaveVersion not bumped; (2) review-pr posts same finding as reviewer comment; (3) false-positive gate: current master (.vxs, .rig, world-snapshot formats) all pass without warnings; (4) engine/asset/CLAUDE.md cross-references the check
  - **Issue:** #670
  - **Notes:** Implements Extensibility Rule #3 enforcement. Blocked by #667 (world snapshot epic) because the check needs real kSaveVersion usage on master to test against without false positives. T-166 (binary-I/O) is already merged (PR #678).
  - **Links:**


- [ ] **prefab: Lua prefab format — entity template referencing .vxs + .rig + component pack** — Prefab.spawn/register API; Lua schema with voxel_ref, rig_ref, components, bind_point_overrides
  - **ID:** T-173
  - **Area:** engine/script, engine/prefabs/irreden
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-170, T-171
  - **Acceptance:** (1) Lua prefab schema: prefab_version=1, voxel_ref, rig_ref (optional), components pack, bind_point_overrides; (2) Prefab.spawn(id, position) C++ API + Lua binding; (3) Prefab.register(id, path) registry; (4) asset path resolution for voxel_ref/rig_ref via existing resolver; (5) round-trip test: register prefab, spawn, verify components attached with correct values, verify entity:bindPoint() returns expected world-space transform; (6) prefab_version enforced on load with clear diagnostic on unknown version; (7) additive component packs load without version bump
  - **Issue:** #671
  - **Notes:** Phase 5 of editor epic (#608). Lua-defined components are out of scope (separate epic). Component palette UI in #608 produces prefab Lua tables matching this schema. New files: prefab_api.hpp, prefab_api.cpp, lua_bindings_prefab.cpp.
  - **Links:**


- [~] **Editor: migrate LayoutState to C_LayoutState singleton component** — replace inline g_layout header variable with a singleton ECS component; update all systems to fetch via getOrCreateSingleton
  - **ID:** T-174
  - **Area:** engine/prefabs/irreden/render, creations/editors
  - **Model:** sonnet
  - **Owner:** claude/T-174-layout-state-singleton
  - **Blocked by:** (none)
  - **Acceptance:** (1) C_LayoutState component defined with all fields from LayoutState struct (nodes_, root_, rootPos_, rootSize_, splitter/panel drag fields); (2) layout.hpp free functions fetch state via IREntity::getOrCreateSingleton<C_LayoutState>(); (3) WIDGET_RENDER_DOCK_PREVIEW filters on C_LayoutState (tick fires once per frame, no-op body removed); (4) inline LayoutState g_layout and struct LayoutState fully removed; (5) IRUiDockspace serialize/deserialize round-trip still logs OK; (6) fleet-build clean on linux-debug
  - **Issue:** #674
  - **Notes:** Escalated from PR #641 (T-148 Opus recheck). The inline g_layout deviates from the engine's prefab-scoped singleton pattern (see fog_of_war.hpp, IREntity::getOrCreateSingleton). Migration is a forward-looking refactor enabling multi-dockspace, serialization via ECS save path, and inspector tooling.
  - **Links:**


- [~] **Move C_Voxel into namespace IRComponents** — restore namespace symmetry with VoxelFlags; update all callers in engine/ and creations/
  - **ID:** T-175
  - **Area:** engine/prefabs/irreden/voxel/components
  - **Model:** sonnet
  - **Owner:** claude/T-175-cvoxel-ircomponents
  - **Blocked by:** (none)
  - **Acceptance:** (1) C_Voxel struct moved into IRComponents namespace in component_voxel.hpp; (2) VoxelFlags in same namespace (symmetry restored); (3) all callers in engine/ and creations/ updated; (4) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #680
  - **Notes:** Pre-existing asymmetry surfaced in PR #635 review. Mechanical rename; touches all callers. No behavioral change.
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-152** — F-0.5 Phase 1 — gizmo primitive geometry · Owner: claude/T-152-gizmo-primitives · PR: https://github.com/jakildev/IrredenEngine/pull/672
- [x] **T-150** — Editor F-0.4 — 3D editor camera (entity rotation + pan + zoom) · Owner: claude/T-150-editor-camera · PR: https://github.com/jakildev/IrredenEngine/pull/660
- [x] **T-160** — TEXT_TO_TRIXEL — hoist gui canvas lookup out of per-entity tick · Owner: claude/T-160-text-trixel-canvas-hoist · PR: https://github.com/jakildev/IrredenEngine/pull/657
- [x] **T-161** — defer C_CanvasFogOfWar dirty-flag → per-region subImage2D migration · Owner: claude/T-161-fog-upload-eval · PR: https://github.com/jakildev/IrredenEngine/pull/652
- [x] **T-149** — Editor F-0.3 — input routing (mouse hover/click/drag, keyboard focus, hotkey table) · Owner: claude/T-149-input-routing · PR: https://github.com/jakildev/IrredenEngine/pull/649
- [x] **T-146** — Editor F-0.6 — per-voxel metadata extension (.txl v2) · Owner: claude/T-146-txl-v2-metadata · PR: https://github.com/jakildev/IrredenEngine/pull/635
- [x] **T-147** — Editor F-0.8 -- editor exe scaffold at creations/editors/voxel_editor/ · Owner: claude/T-147-voxel-editor-scaffold · PR: https://github.com/jakildev/IrredenEngine/pull/634
- [x] **T-148** — Editor F-0.2 — layout system (rows, columns, dock targets, splitters) · Owner: claude/T-148-layout-system · PR: https://github.com/jakildev/IrredenEngine/pull/641
- [x] **T-159** — GPU particles Phase 2 — batch CPU-side spawns into one subData/frame · Owner: claude/T-159-gpu-particle-spawn-batching · PR: https://github.com/jakildev/IrredenEngine/pull/651
- [x] **T-162** — engine/entity: ECS singleton-component infrastructure · Owner: claude/T-162-ecs-singleton · PR: https://github.com/jakildev/IrredenEngine/pull/650
- [x] **T-157** — Migrate lighting + debug cluster to member-on-System<N> · Owner: claude/T-157-lighting-debug-register-system · PR: https://github.com/jakildev/IrredenEngine/pull/640
- [x] **T-158** — Migrate final composite + sprites cluster to member-on-System<N> · Owner: claude/T-158-final-composite-register-system · PR: https://github.com/jakildev/IrredenEngine/pull/639
- [x] **T-156** — Migrate trixel-canvas content systems to member-on-System<N> · Owner: claude/T-156-trixel-canvas-register-system · PR: https://github.com/jakildev/IrredenEngine/pull/638
- [x] **T-155** — Migrate GPU-compute cluster to member-on-System<N> · Owner: claude/T-155-gpu-compute-register-system · PR: https://github.com/jakildev/IrredenEngine/pull/637
- [x] **T-154** — Migrate hitbox GUI system to member-on-System<N> · Owner: claude/T-154-hitbox-gui-register-system · PR: https://github.com/jakildev/IrredenEngine/pull/636
- [x] **T-145** — Editor F-0.1: trixel UI primitives · Owner: claude/T-145-trixel-ui-primitives · PR: https://github.com/jakildev/IrredenEngine/pull/631
- [x] **T-144** — Docs: land entity-editor-epic.md canonical reference · Owner: claude/T-144-entity-editor-epic-doc · PR: https://github.com/jakildev/IrredenEngine/pull/630
- [x] **T-143** — Render: cache resolved sun direction once per frame · Owner: claude/T-143-resolve-sun-direction · PR: https://github.com/jakildev/IrredenEngine/pull/615
- [x] **T-141** — Demo: Z-Yaw world rotation showcase · Owner: claude/T-141-z-yaw-rotation-demo · PR: https://github.com/jakildev/IrredenEngine/pull/602
- [x] **T-142** — macOS — fix IRShapeDebug crash in UPDATE_VOXEL_SET_CHILDREN · Owner: claude/T-142-voxel-set-children-crash · PR: https://github.com/jakildev/IrredenEngine/pull/601
