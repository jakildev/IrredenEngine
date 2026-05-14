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

- [ ] **test: JsonSidecarWriter + NameTable round-trips** — add dedicated unit tests for engine/asset json_sidecar and name_table, covering edge cases not exercised by existing .vxs integration tests
  - **ID:** T-186
  - **Area:** engine/asset
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) test/asset/json_sidecar_test.cpp added and registered — covers empty object/array, scope nesting, all value* overloads, string escaping, numeric edge cases, NaN/Inf contract, key ordering, and file-open failure; (2) test/asset/name_table_test.cpp added and registered — covers write/read round-trip (0/1/many entries), non-ASCII names, idByName/nameById hits/misses, duplicate-insert behavior, empty-name edge case; (3) all new tests pass; no production-code changes; fleet-build clean on linux-debug
  - **Issue:** #707
  - **Notes:** Engine/asset audit found JsonSidecarWriter and NameTable exercised only indirectly via voxel_set_hybrid_test.cpp. Use MemoryBinaryWriter/Reader for name-table tests and writer's internal str() for sidecar tests — do not depend on .vxs in these tests. Lock NaN/Inf behavior as-is (whatever writer currently does). If any existing test asserts a hard-coded test count, update it.
  - **Links:**

- [ ] **render: LOD Phase 1 — wire computeLodLevel + per-shape lodMin filter** — promote lod_utils.hpp stub to live; add LOD_UPDATE system + C_ActiveLodLevel singleton; CPU-side per-shape filter in SHAPES_TO_TRIXEL staging
  - **ID:** T-187
  - **Area:** engine/render, engine/prefabs/irreden/render, engine/system
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) lod_utils.hpp exits stub status; computeLodLevel/shouldSkipAtLod/lodVoxelScale called by live systems with thresholds matching real zoom range (1.0–64.0); (2) LOD_UPDATE system added to SystemName enum and UPDATE pipeline; writes C_ActiveLodLevel singleton each frame from C_ZoomLevel; (3) C_ShapeDescriptor::lodLevel_ renamed to lodMin_ or semantics documented in header; (4) SHAPES_TO_TRIXEL reads C_ActiveLodLevel once at beginTick and skips shapes with lodMin_ < activeLod; (5) render-verify shot confirms progressive shape-count change across zoom 1.0/2.0/4.0/8.0; (6) no DENSE-mode, no .rig, no shader changes; fleet-build clean on linux-debug and macos-debug
  - **Issue:** #708
  - **Notes:** lod_utils.hpp self-describes as "WIP stub — not yet included or referenced by any system". C_ShapeDescriptor::lodLevel_ already shipped to GPU as GPUShapeDescriptor.lodLevel but shader ignores it. Phase 1 filter is CPU-side only — GPU struct unchanged (Metal/GL parity hazard). Do NOT implement Phase 2 (per-tier .vxs refs from prefab manifest) or Phase 3 (cross-tier interpolation). Design rationale in docs/design/lod-strategy.md (PR #710).
  - **Links:**

- [ ] **script: decouple IrredenEngineScripting from IrredenEngineRendering** — fix layering violation where prefab_api.cpp rig loading pulls in C_JointHierarchy/GPUJointTransform header chain, causing scripting to link rendering
  - **ID:** T-188
  - **Area:** engine/script, engine/prefabs/irreden/render, engine/asset
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) IrredenEngineScripting target no longer links against IrredenEngineRendering (verify via CMake dependency graph or linker map); (2) fleet-build clean on linux-debug; (3) IrredenEngineTest passes; no functional regressions
  - **Issue:** #709
  - **Notes:** Layering violation surfaced in T-173 / PR #703. Two viable fixes: (A) hoist GPUJointTransform into a non-render header so script does not pull in the full render module; (B) move setComponent(entity, Rig::toComponent(...)) to a higher-level layer that can depend on both script and render. Author chooses approach; document rationale in commit message.
  - **Links:**

- [ ] **prefab: DENSE/HYBRID voxel_ref ECS attachment (headless C_VoxelSetNew)** — architect decision + implementation for attaching DENSE/HYBRID-mode .vxs data as C_VoxelSetNew in Prefab.spawn without requiring an active render canvas
  - **ID:** T-189
  - **Area:** engine/prefabs/irreden/voxel, engine/script
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-182
  - **Acceptance:** (1) DENSE .vxs voxel_ref attaches per-voxel data as C_VoxelSetNew on spawned entity, headless-safe; (2) HYBRID .vxs attaches both SHAPES (T-182) and DENSE halves on same entity; (3) round-trip tests: DENSE and HYBRID verified through Prefab.spawn; entity C_VoxelSetNew record count matches dense_.voxelCount(); (4) engine/prefabs/irreden/voxel/CLAUDE.md and engine/script/CLAUDE.md document chosen attachment contract
  - **Issue:** #721
  - **Notes:** SHAPES half wired in T-182 / PR #718; DENSE/HYBRID deferred. Two design options: (A) pool-injectable ctor — C_VoxelSetNew takes DenseVoxelSet + explicit pool entity id (kNullEntity for headless), later step seeds pool when canvas activates; (B) lazy attach — component stores DenseVoxelSet payload, UPDATE-pipeline system seeds pool the frame after canvas activates. Architect chooses; document in CLAUDE.md. Parent epic: #608. Asset side: T-167/PR#691 (DENSE), T-170/PR#694 (HYBRID).
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-104** — Lua-driven ECS: Lua port of perf_grid + perf parity gate · PR: https://github.com/jakildev/IrredenEngine/pull/563
- [x] **T-183** — asset: hoist vec3/vec4 + color binary I/O helpers into engine/math/ · Owner: claude/T-183-math-binary-io-helpers · PR: https://github.com/jakildev/IrredenEngine/pull/719
- [x] **T-176** — GPU particles: port stateless-particles 2x3 voxel-diamond render fix · Owner: claude/T-176-gpu-particles-voxel-diamond · PR: https://github.com/jakildev/IrredenEngine/pull/699
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
- [x] **T-172** — tooling: simplify + review-pr serialized-struct version-bump check · Owner: claude/T-172-serialized-struct-version-check · PR: https://github.com/jakildev/IrredenEngine/pull/688
- [x] **T-164** — F-0.5 Phase 2 — screen-space gizmo sizing + depth-aware dimming · Owner: claude/T-164-gizmo-screen-space · PR: https://github.com/jakildev/IrredenEngine/pull/677
- [x] **T-168** — asset: .vxs v1 shape-group save format (SHPG, SREF, MODE chunks) · Owner: claude/T-168-vxs-shape-group · PR: https://github.com/jakildev/IrredenEngine/pull/679
