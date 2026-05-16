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
  - **Owner:** claude/T-199-migrate-to-world-transform
  - **Blocked by:** (none)
  - **Stack:** T-197..T-199 transform-consolidation
  - **Acceptance:** (1) `grep -r "C_PositionGlobal3D"` and `grep -r "C_Position3D"` return only references in this ticket's deletion commits; (2) `grep -r "C_Rotation"` cleaned up; (3) every consumer reads `C_WorldTransform` (or `C_LocalTransform` for write paths); (4) `IRShapeDebug` render-debug-loop shot list passes pre/post-migration; (5) IRVoxelEditor/current editor demo functions: gizmos move, voxels position correctly, hitboxes resolve; (6) no regressions in tests: `test/ecs/*`, `test/asset/*`; (7) `engine/prefabs/irreden/common/CLAUDE.md` and `engine/prefabs/irreden/voxel/CLAUDE.md` updated to describe post-migration model; (8) verified on linux-debug (OpenGL) and macos-debug (Metal)
  - **Issue:** #736
  - **Notes:** Part of epic #731 (transform consolidation, Phase 2). Likely too large for one PR — consider splitting by subsystem: render-side → input-side → voxel-side → final retirement. Key gotcha: `C_VoxelPool`'s SoA layout currently carries `{C_Position3D, C_PositionOffset3D, C_PositionGlobal3D}` arrays — decide during impl whether to use one `C_WorldTransform` array or keep position-only views as cached projections. Lua bindings (sol2 + `*_lua.hpp` files) may need updating. GPU-side shape descriptor stays position-only; convert SQT→position on CPU before staging. Animation systems (sprite UV) not affected but audit `C_AnimationClip` / `C_ActionAnimation`.
  - **Links:**

- [~] **editor 2.1: joints as entities with CHILD_OF relations (replace SoA C_JointHierarchy)** — declare `C_Skeleton` + `C_Joint` components; deprecate `C_JointHierarchy`; document entity-based joint model in CLAUDE.md and design docs
  - **ID:** T-200
  - **Area:** engine/prefabs/irreden/voxel, docs
  - **Model:** opus
  - **Owner:** claude/T-200-skeleton-joint-entities
  - **Blocked by:** (none)
  - **Acceptance:** (1) `C_Skeleton` and `C_Joint` components compile and register; (2) `C_JointHierarchy` header has a deprecation comment pointing at `C_Skeleton` with a brief migration note for #605 implementers; (3) `engine/prefabs/irreden/voxel/CLAUDE.md` describes the entity-based joint model and severance design; (4) `#605`'s body or the linked design doc (`docs/design/entity-editor-epic.md`) reflects the refined 2.1 approach; (5) fleet-build clean on linux-debug
  - **Issue:** #737
  - **Notes:** Architectural foundation only — no GPU upload system, no animation, no severance API yet. Those live in #605 Phase 2 (after SQT propagation from T-197 and editor Phase 1 from #604 land). Key design point: `C_Skeleton.joints_` is a flat ordered list (canonical bone-index space); indices are stable across saves, severance leaves a hole rather than shifting. This ticket should NOT bake assumptions about which transform component joints carry (SQT not landed yet); leave joint-entity transform type unspecified in the header with a comment. Coordinates with #731 (joint propagation reuses `SYSTEM_PROPAGATE_TRANSFORM`).
  - **Links:**

- [ ] **script: complete T-188 layering — decouple prefab_api.cpp + shape descriptor from IRRender** — remove the residual `IrredenEngineRendering` link from `engine/script/` by moving `ShapeType`, `getActiveCanvasEntityOrNull`, and voxel pool allocator to render-neutral headers/modules
  - **ID:** T-201
  - **Area:** engine/script, engine/render, engine/prefabs/irreden/voxel, engine/math, engine/world
  - **Model:** opus
  - **Owner:** free
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

- [~] **render: Linux/OpenGL backend parity — gcc-13 compile + GLSL shaders + trixel pipeline** — verify and fix the engine/render OpenGL path on linux-debug (WSL2/Ubuntu 24.04/gcc-13) so it compiles clean, all compute shaders load, and the trixel/lighting/camera pipeline matches the leading backend
  - **ID:** T-202
  - **Area:** engine/render, shaders/glsl
  - **Model:** opus
  - **Owner:** opus-worker-1
  - **Blocked by:** (none)
  - **Acceptance:** (1) `fleet-build --target IRShapeDebug` succeeds on linux-debug (gcc-13, OpenGL); (2) all GLSL compute shaders in `engine/render/src/shaders/` load and dispatch without GL errors; (3) trixel pipeline (canvas → composite → framebuffer), lighting stage, and camera/coordinate transform produce output matching the Metal reference at `shape_debug` level; (4) `render-debug-loop` oracle passes on linux-debug; (5) Metal-only features documented (not ported) in engine/render/CLAUDE.md; (6) fleet-build clean on linux-debug
  - **Issue:** #757
  - **Notes:** Linux WSL2 host recently upgraded from Ubuntu 20.04 (gcc-9) to 24.04 (gcc-13). This is the engine-side prerequisite for demo validation (T-203). Focus on any `#ifdef`/platform divergence in the OpenGL path, GLSL version compatibility, compute shader dispatch grids. Backend-parity skill for Metal follow-ups. See `docs/agents/BUILD.md` for the linux-debug preset and `fleet-build` / `fleet-run` wrappers.
  - **Links:**

- [ ] **build/demos: Linux demo validation suite — build + run + screenshot all demos on linux-debug** — for each demo listed in #757, confirm it builds clean and renders a correct frame on linux-debug, commit reference screenshots, and open per-demo PRs
  - **ID:** T-203
  - **Area:** creations/demos, build
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-202
  - **Acceptance:** (1) every demo in #757's in-scope list either (a) builds + runs + renders a correct frame on linux-debug, or (b) is explicitly documented as unsupported (metal_clear_test excluded on Linux, midi_keyboard WSLg-unsupported); (2) reference screenshots committed under docs/pr-screenshots/ref/linux/ or wherever `render-verify` expects them; (3) per-demo PRs include screenshots in the body; (4) shape_debug fixed first (it is the render-debug-loop oracle)
  - **Issue:** #757
  - **Notes:** One PR per demo is the right granularity. Use the `render-debug-loop` and `attach-screenshots` skills. After T-202 lands, Sonnet workers can pick demos in parallel. The `fleet:needs-linux-smoke` label triggers cross-host smoke validation automatically.
  - **Links:**

- [~] **entity: fix sortArchetypeNodesByRelationChildOf — BFS seeds leaves instead of roots, drops parent archetypes** — invert BFS to seed from true roots (no CHILD_OF) and walk outward so all archetypes including parents appear in sorted output
  - **ID:** T-204
  - **Area:** engine/entity
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-2
  - **Blocked by:** (none)
  - **Acceptance:** (1) BFS seeded from archetypes with `getChildOfRelation() == kNullRelation` (true roots), walks outward via children; (2) `sortedNodes` contains all archetypes including multi-level parents, not just leaf archetypes; (3) existing `test/ecs/` suite passes; (4) new test in `test/ecs/` exercises a 3-level CHILD_OF chain through relational dispatch and verifies root entities are dispatched first; (5) fleet-build clean on linux-debug
  - **Issue:** #750
  - **Notes:** Traced during T-197 (#749). The set `childNodes` is misnamed — it collects parent NodeIds (archetypes pointed at as CHILD_OF parents). Step 2 then queues nodes NOT in that set (i.e., leaves), causing parents to never enter `sortedNodes`. Fix: rename variable to `parentNodeIds`, seed queue with nodes where `getChildOfRelation() == kNullRelation`, walk from roots via `getParentNodeFromRelation`. Two known consumers — `GLOBAL_POSITION_3D` and `TRIXEL_TO_TRIXEL` — are unaffected today but any future relational system needing ALL matching entities will be silently wrong. `SYSTEM_PROPAGATE_TRANSFORM` (T-197) already works around this with its own topo-sort. Touch surface: `engine/entity/src/archetype_graph.cpp:55` and `test/ecs/`.
  - **Links:**

- [~] **script: architect decision + port — move getActiveCanvasEntityOrNull out of ir_render.hpp (T-201 step 2)** — architect picks destination (IRWorld or caller-threading), implement include shuffle + update ~3 call sites so component ctors no longer include ir_render.hpp for canvas snapshot
  - **ID:** T-205
  - **Area:** engine/render, engine/world, engine/prefabs/irreden/voxel, engine/script
  - **Model:** opus
  - **Owner:** opus-worker-2
  - **Blocked by:** (none)
  - **Acceptance:** (1) `ir_render.hpp` no longer exposes `getActiveCanvasEntityOrNull`; (2) `component_shape_descriptor.hpp` and `component_voxel_set.hpp` no longer include `<irreden/ir_render.hpp>` for the canvas snapshot; (3) `IrredenEngineTest` and `IRShapeDebug` build and run; (4) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #753
  - **Notes:** Step 2 of the T-201 four-PR layering refactor. Step 1 (ShapeFlags → IRMath::SDF) landed as PR #752. Two options the architect must decide: (a) move function to `IRWorld::` — ambient-snapshot ergonomics preserved, sourced from world-level singleton instead of render; (b) push snapshot up to callers — `Prefab.spawn` threads active canvas into `C_ShapeDescriptor`/`C_VoxelSetNew` ctors. Call sites: `component_shape_descriptor.hpp:40,46` and `component_voxel_set.hpp:148`. Implementation is ~include shuffle + 3 call-site updates. T-201's final step (drop `IrredenEngineRendering` from script link) is blocked on this landing.
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

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
- [x] **T-181** — prefab/runtime: C_BindPoints + entity:bindPoint Lua API · Owner: claude/T-181-bind-points-runtime · PR: https://github.com/jakildev/IrredenEngine/pull/720
- [x] **T-186** — test: JsonSidecarWriter + NameTable round-trips · Owner: claude/T-186-json-sidecar-name-table-tests · PR: https://github.com/jakildev/IrredenEngine/pull/730
- [x] **T-185** — asset: small cleanups — ShapeRecord serialized annotation, dead stub, makeTag length assert, CLAUDE.md refresh · Owner: claude/T-185-asset-cleanups · PR: https://github.com/jakildev/IrredenEngine/pull/726
- [x] **T-188** — script: decouple IrredenEngineScripting from IrredenEngineRendering · Owner: claude/T-188-decouple-scripting-rendering · PR: https://github.com/jakildev/IrredenEngine/pull/723
- [x] **T-184** — asset: delete entire .txl family (raw-binary + .txl.json sidecar + nlohmann dep) · Owner: claude/T-184-delete-txl-family · PR: https://github.com/jakildev/IrredenEngine/pull/722
- [x] **T-182** — prefab: attach voxel_ref data as ECS components on Prefab.spawn · Owner: claude/T-182-prefab-voxel-attach · PR: https://github.com/jakildev/IrredenEngine/pull/718
- [x] **T-183** — asset: hoist vec3/vec4 + color binary I/O helpers into engine/math/ · Owner: claude/T-183-math-binary-io-helpers · PR: https://github.com/jakildev/IrredenEngine/pull/719
- [x] **T-173** — prefab: Lua prefab format — Prefab.register/spawn + schema validation · Owner: claude/T-173-prefab-lua-format · PR: https://github.com/jakildev/IrredenEngine/pull/703
- [x] **T-178** — engine/entity singleton reentrancy guard doc + cache-reset test · Owner: claude/T-178-singleton-reentrancy-doc · PR: https://github.com/jakildev/IrredenEngine/pull/713
- [x] **T-179** — asset: canonicalize memcpy in binary_io + voxel_set_format (bit_cast + chunk-tag helpers) · Owner: claude/T-179-asset-bit-cast-tag-helpers · PR: https://github.com/jakildev/IrredenEngine/pull/712
