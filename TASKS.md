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


- [~] **GPU particles: port stateless-particles 2×3 voxel-diamond render fix** — port the 6-trixel per-particle emit loop from stateless-particles shader to c_render_gpu_particles_to_trixel (GLSL + Metal) so GPU-pool particles render as lit voxel diamonds
  - **ID:** T-176
  - **Area:** shaders/glsl, shaders/metal
  - **Model:** sonnet
  - **Owner:** claude/T-176-gpu-particles-voxel-diamond
  - **Blocked by:** (none)
  - **Acceptance:** (1) IRGpuParticles (or any creation using C_GPUParticlePool with a lighting pipeline) renders particles as 3-face voxel diamonds with correct 3-tone shading; (2) fleet-build clean on linux-debug and macos-debug; (3) shaders pass render-debug-loop; (4) GLSL and Metal emit the same trixel set per particle (no backend-parity drift)
  - **Issue:** #689
  - **Notes:** Mechanical port — replace single-pixel imageAtomicMin/imageStore block in c_render_gpu_particles_to_trixel.glsl/.metal with the same `for face / for subPixel` loop from c_render_stateless_particles_to_trixel (T-163 PR #659). Keep local_size_x=64 dispatch shape; dead-slot early-out (lifetime <= 0) stays before the emit loop. Metal: atomic_int distanceScratch path unchanged. Filed as follow-up to human review on PR #659.
  - **Links:**

- [~] **prefab/runtime: C_BindPoints runtime component + entity:bindPoint() Lua API** — wire .rig BIND chunk to runtime ECS component; expose entity:bindPoint("name") world-space transform via Lua
  - **ID:** T-181
  - **Area:** engine/prefabs/irreden/voxel, engine/script
  - **Model:** opus
  - **Owner:** claude/T-181-bind-points-runtime
  - **Blocked by:** (none)
  - **Acceptance:** (1) C_BindPoints runtime component declared and registered; (2) IRPrefab::Rig::toBindPoints(asset::Rig) bridge function; (3) C_BindPoints Lua binding via standard *_lua.hpp pattern; (4) LuaEntity::bindPoint(name) returns world-space transform (offset + rotation); (5) Prefab.spawn attaches C_BindPoints from rig_ref automatically, applies bind_point_overrides from prefab table; (6) round-trip test: entity:bindPoint("named_anchor") matches expected joint chain result; override changes result; (7) engine/prefabs/irreden/voxel/CLAUDE.md and engine/script/CLAUDE.md document new surface + per-frame-cost contract
  - **Issue:** #700
  - **Notes:** Phase 5.1 + 5.3 of editor epic #608. Deferred from T-173 / PR #671 (no runtime C_BindPoints component existed). Asset side: T-171 / PR #686 (BIND chunk). Per-frame cost: document unordered_map<string,...> lookup as one-time query at spawn/interaction, not per-tick; integer-handle escape hatch for hot use cases deferred.
  - **Links:**

- [~] **prefab: attach voxel_ref data as ECS components on Prefab.spawn** — route loaded .vxs records to runtime C_ShapeDescriptor / C_VoxelSetNew on the spawned entity; SHAPES and DENSE/HYBRID modes
  - **ID:** T-182
  - **Area:** engine/script, engine/prefabs/irreden/voxel
  - **Model:** opus
  - **Owner:** claude/T-182-prefab-voxel-attach
  - **Blocked by:** (none)
  - **Acceptance:** (1) SHAPES-mode .vxs files attach per-record C_ShapeDescriptor to spawned entity; (2) DENSE/HYBRID-mode .vxs files attach without requiring active canvas (or contract explicitly documented if deferred); (3) round-trip test: register prefab with known .vxs, spawn, verify expected C_ShapeDescriptor records / C_VoxelSetNew slot count on entity; (4) engine/script/CLAUDE.md and engine/prefabs/irreden/voxel/CLAUDE.md document attachment behavior
  - **Issue:** #701
  - **Notes:** Phase 5 of editor epic #608. Deferred from T-173 / PR #671. Two phases: (1) SHAPES: C_ShapeDescriptor per shapeRecords_ entry (offset/rotation/csgOp/boneId); architect decides entity shape (child vs list on parent). (2) DENSE/HYBRID: headless-friendly C_VoxelSetNew constructor — either passed-in pool or lazy-attach deferred until canvas active. Asset side: T-170 / PR #694 (hybrid .vxs loader).
  - **Links:**

- [~] **asset: hoist vec3/vec4 + color binary I/O helpers into engine/math/** — centralize duplicated writeVec3/writeVec4/readVec3/readVec4 and color pack/unpack helpers into IRMath::BinaryIO; adopt in voxel_set_format.cpp and rig_format.cpp
  - **ID:** T-183
  - **Area:** engine/math, engine/asset
  - **Model:** sonnet
  - **Owner:** claude/T-183-math-binary-io-helpers
  - **Blocked by:** (none)
  - **Acceptance:** (1) zero copies of vec3/vec4 / color pack helpers remain inside engine/asset/src/*.cpp; (2) IRMath::BinaryIO (or equivalent engine/asset/include location) exposes writeVec3/writeVec4/readVec3/readVec4 and Color::fromPackedRGBA; (3) engine/math/ does not physically depend on engine/asset/; (4) voxel_set_format.cpp and rig_format.cpp call through the shared helpers; (5) all existing tests pass; (6) fleet-build clean on linux-debug
  - **Issue:** #704
  - **Notes:** Two duplicated families: (1) writeVec3/writeVec4/readVec3/readVec4 defined in voxel_set_format.cpp anonymous namespace and reimplemented as encode/decode* in rig_format.cpp — standardize on read/write naming; (2) writeColorPacked/unpackColor in voxel_set_format.cpp — Color::fromPackedRGBA alongside existing toPackedRGBA. If dependency direction is awkward, put helpers in engine/asset/include/irreden/asset/math_binary_io.hpp rather than engine/math/ — point is one canonical location. No glm:: in helper signatures (IRMath::vec* only).
  - **Links:**

- [~] **asset: delete entire .txl family (raw-binary + .txl.json sidecar + nlohmann dep)** — remove all .txl I/O code, C_TriangleCanvasTextures file methods, debug command, txl_sidecar test, and nlohmann/json fetch
  - **ID:** T-184
  - **Area:** engine/asset, engine/prefabs/irreden/render
  - **Model:** sonnet
  - **Owner:** claude/T-184-delete-txl-family
  - **Blocked by:** (none)
  - **Acceptance:** (1) engine/asset/ and engine/prefabs/irreden/ contain no reference to .txl, TxlSidecar, saveTxlSidecar, loadTxlSidecar, saveTrixelTextureData, loadTrixelTextureData, kTrixelExtension, kTxlSidecarExtension, or kTrixelImage; (2) C_TriangleCanvasTextures has no saveToFile/loadFromFile methods; (3) command_save_main_canvas_trixels.hpp and test/asset/txl_sidecar_test.cpp deleted; (4) nlohmann/json no longer fetched by the engine build; (5) engine/asset/CLAUDE.md no longer documents .txl or .txl.json; (6) IRShapeDebug and IrredenEngineTest build clean
  - **Issue:** #705
  - **Notes:** .txl superseded by .vxs (T-167/168/170) — saveTrixelTextureData still v1 raw-binary with no header, saveTxlSidecar/loadTxlSidecar have zero non-test callers, nlohmann/json is exclusively used by txl. Removal steps: (a) delete IRAsset entry points (header + impl); (b) delete C_TriangleCanvasTextures::saveToFile/loadFromFile; (c) delete command_save_main_canvas_trixels.hpp; (d) delete test/asset/txl_sidecar_test.cpp; (e) drop nlohmann/json FetchContent + linkage from CMakeLists.txt; (f) update CLAUDE.md. FileTypes enum shift (kVoxelImage 2→1) is safe — enum unused at runtime. .irsprite stays untouched.
  - **Links:**

- [ ] **asset: small cleanups — ShapeRecord serialized annotation, dead stub, makeTag length assert, CLAUDE.md refresh** — add // IRAsset: serialized to ShapeRecord, delete ir_asset_types.hpp stub, assert on makeTag input length, refresh CLAUDE.md
  - **ID:** T-185
  - **Area:** engine/asset
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-184
  - **Acceptance:** (1) ShapeRecord carries `// IRAsset: serialized` + `static constexpr uint16_t kSaveVersion = kShapeRecordVersion;`; (2) other directly-serialized structs in engine/asset/ (RigJoint, RigBindPoint, dense per-voxel record) audited and annotated; (3) ir_asset_types.hpp deleted with no orphaned includes; (4) makeTag asserts (or static_asserts) on s.length() != 4; (5) engine/asset/CLAUDE.md opener updated to reflect current scope (not "Tiny module"); .txl gotcha bullet removed; .rig entry points listed; (6) fleet-build clean; T-172 serialized-struct linter passes
  - **Issue:** #706
  - **Notes:** Bundle of mechanical items from audit pass. makeTag is constexpr — prefer static_assert if all call sites use string-literal constexpr args, otherwise runtime assert. Sequence CLAUDE.md update after T-184 (.txl deletion) merges — rebase if T-184 lands first. ir_asset_types.hpp is an empty header (ifndef/define/endif only); grep for includes before removing.
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-173** — prefab: Lua prefab format — Prefab.register/spawn + schema validation · Owner: claude/T-173-prefab-lua-format · PR: https://github.com/jakildev/IrredenEngine/pull/703
- [x] **T-178** — engine/entity singleton reentrancy guard doc + cache-reset test · Owner: claude/T-178-singleton-reentrancy-doc · PR: https://github.com/jakildev/IrredenEngine/pull/713
- [x] **T-179** — asset: canonicalize memcpy in binary_io + voxel_set_format (bit_cast + chunk-tag helpers) · Owner: claude/T-179-asset-bit-cast-tag-helpers · PR: https://github.com/jakildev/IrredenEngine/pull/712
- [x] **T-180** — asset: hoist .vxs.json sidecar keys + VoxelSetMode string to named constants · Owner: claude/T-180-sidecar-key-constants · PR: https://github.com/jakildev/IrredenEngine/pull/711
- [x] **T-177** — F-0.1 follow-up — remaining widgets (list, dropdown, radio, text input, scroll) · Owner: claude/T-177-widget-followup · PR: https://github.com/jakildev/IrredenEngine/pull/702
- [x] **T-175** — Move C_Voxel into namespace IRComponents · Owner: claude/T-175-cvoxel-ircomponents · PR: https://github.com/jakildev/IrredenEngine/pull/696
- [x] **T-174** — Editor: migrate LayoutState to C_LayoutState singleton component · Owner: claude/T-174-layout-state-singleton · PR: https://github.com/jakildev/IrredenEngine/pull/695
- [x] **T-170** — asset: .vxs hybrid mode + sidecar emitter + full test suite · Owner: claude/T-170-vxs-hybrid-sidecar · PR: https://github.com/jakildev/IrredenEngine/pull/694
- [x] **T-171** — asset: .rig v2 — bind-points (BIND) chunk; persist C_BindPoints · Owner: claude/T-171-rig-v2-bind-chunk · PR: https://github.com/jakildev/IrredenEngine/pull/686
- [x] **T-153** — Editor F-0.9 — voxel mouse picking (cursor→ray, DDA, single selection) · Owner: claude/T-153-voxel-picking · PR: https://github.com/jakildev/IrredenEngine/pull/682
- [x] **T-167** — .vxs v1 dense-mode reader/writer (BNDS, VOXR, LAYR, FRAM, META chunks) · Owner: claude/T-167-vxs-dense · PR: https://github.com/jakildev/IrredenEngine/pull/691
- [x] **T-165** — Editor F-0.5 Phase 3 — gizmo hover + drag interaction · Owner: claude/T-165-gizmo-hover-drag · PR: https://github.com/jakildev/IrredenEngine/pull/685
- [x] **T-169** — asset: .rig v1 — joints (JNTS) chunk; persist C_JointHierarchy · Owner: claude/T-169-rig-v1 · PR: https://github.com/jakildev/IrredenEngine/pull/681
- [x] **T-151** — Editor F-0.7 — JSON sidecar format for .txl · Owner: claude/T-151-txl-json-sidecar · PR: https://github.com/jakildev/IrredenEngine/pull/661
- [x] **T-163** — Stateless procedural particle system — UBO-driven emitters · Owner: claude/T-163-stateless-particles · PR: https://github.com/jakildev/IrredenEngine/pull/659
- [x] **T-172** — tooling: simplify + review-pr serialized-struct version-bump check · Owner: claude/T-172-serialized-struct-version-check · PR: https://github.com/jakildev/IrredenEngine/pull/688
- [x] **T-164** — F-0.5 Phase 2 — screen-space gizmo sizing + depth-aware dimming · Owner: claude/T-164-gizmo-screen-space · PR: https://github.com/jakildev/IrredenEngine/pull/677
- [x] **T-168** — asset: .vxs v1 shape-group save format (SHPG, SREF, MODE chunks) · Owner: claude/T-168-vxs-shape-group · PR: https://github.com/jakildev/IrredenEngine/pull/679
- [x] **T-152** — F-0.5 Phase 1 — gizmo primitive geometry · Owner: claude/T-152-gizmo-primitives · PR: https://github.com/jakildev/IrredenEngine/pull/672
- [x] **T-150** — Editor F-0.4 — 3D editor camera (entity rotation + pan + zoom) · Owner: claude/T-150-editor-camera · PR: https://github.com/jakildev/IrredenEngine/pull/660
