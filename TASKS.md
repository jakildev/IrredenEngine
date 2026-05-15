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


- [~] **prefab: DENSE/HYBRID voxel_ref ECS attachment (headless C_VoxelSetNew)** — architect decision + implementation for attaching DENSE/HYBRID-mode .vxs data as C_VoxelSetNew in Prefab.spawn without requiring an active render canvas
  - **ID:** T-189
  - **Area:** engine/prefabs/irreden/voxel, engine/script
  - **Model:** opus
  - **Owner:** claude/T-189-prefab-dense-attach
  - **Blocked by:** (none)
  - **Acceptance:** (1) DENSE .vxs voxel_ref attaches per-voxel data as C_VoxelSetNew on spawned entity, headless-safe; (2) HYBRID .vxs attaches both SHAPES (T-182) and DENSE halves on same entity; (3) round-trip tests: DENSE and HYBRID verified through Prefab.spawn; entity C_VoxelSetNew record count matches dense_.voxelCount(); (4) engine/prefabs/irreden/voxel/CLAUDE.md and engine/script/CLAUDE.md document chosen attachment contract
  - **Issue:** #721
  - **Notes:** SHAPES half wired in T-182 / PR #718; DENSE/HYBRID deferred. Two design options: (A) pool-injectable ctor — C_VoxelSetNew takes DenseVoxelSet + explicit pool entity id (kNullEntity for headless), later step seeds pool when canvas activates; (B) lazy attach — component stores DenseVoxelSet payload, UPDATE-pipeline system seeds pool the frame after canvas activates. Architect chooses; document in CLAUDE.md. Parent epic: #608. Asset side: T-167/PR#691 (DENSE), T-170/PR#694 (HYBRID).
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

- [~] **prefabs: vec3 modifier kind — extend modifier compose for vector fields** — add vec3-typed parameter support to the modifier system so position perturbations can be expressed as one logical modifier instead of three per-axis scalar ones
  - **ID:** T-191
  - **Area:** engine/prefabs/irreden/common
  - **Model:** opus
  - **Owner:** claude/T-191-vec3-modifier-kind
  - **Blocked by:** (none)
  - **Acceptance:** (1) `IRPrefab::Modifier::push<vec3>()` or equivalent accepted by modifier system; (2) `C_ResolvedFields` or a vec3-typed companion yields a composed vec3 value for a given field; (3) compose order matches scalar semantics: OVERRIDE clears prior ops, ADD/MULTIPLY stack, CLAMP applied last, component-wise throughout; (4) existing scalar modifier tests (`test/ecs/modifier_runtime_test.cpp`, `modifier_lua_test.cpp`) still pass; (5) new tests cover vec3 ADD stacking, OVERRIDE-clears-prior, MULTIPLY scaling, per-axis clamp; (6) `engine/prefabs/irreden/common/CLAUDE.md` documents the typed-field model; (7) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #732
  - **Notes:** Scalar-only modifier system forces vec3 perturbations (idle bob, knockback, screen shake) to fan out to three per-axis modifiers or use a dedicated hand-rolled component (the C_PositionOffset3D pattern deleted in T-192). Architect picks between (A) tagged-union param — `std::variant<float, vec3, quat>` in Modifier::param_ (preferred long-term; check trivial-copyability invariant) or (B) parallel ModifierVec3/C_ModifiersVec3 types (cheaper, less invasive). quat modifier kind is out of scope (Phase 2, sibling under #731). Lua API extension deferred unless straightforward. LambdaModifier::fn_ signature must also be extended if option A is chosen. Parent epic: #731.
  - **Links:**

- [ ] **prefabs: delete C_PositionOffset3D — migrate idle bob + gizmo offset to vec3 modifiers** — remove the hand-rolled per-frame additive offset component; rewrite its two writers as vec3 modifier pushes and update the four reader systems to drop the manual globalPos+offset sum
  - **ID:** T-192
  - **Area:** engine/prefabs/irreden/common, engine/prefabs/irreden/update, engine/prefabs/irreden/render, engine/prefabs/irreden/input, engine/entity
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-191
  - **Acceptance:** (1) `grep -r "C_PositionOffset3D"` returns nothing in the codebase; (2) idle-bob entity in any demo visually identical pre/post-migration; (3) gizmo drag in editor still functions; (4) hitbox mouse-test resolves to the correct entity at the correct world position; (5) sprites render at correct screen position including any active idle offset; (6) `createEntity` no longer auto-attaches the offset component; (7) no system reads `globalPos + offset` — manual-sum pattern is gone; (8) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #733
  - **Notes:** C_PositionOffset3D predates the modifier system and is a hand-rolled position modifier channel. Two writers: system_periodic_idle_position_offset (overwrites each tick) and system_gizmo_drag (reads at drag begin). Four readers: system_sprites_to_screen, system_apply_position_offset, system_hitbox_mouse_test, system_entity_canvas_to_framebuffer. Key ordering gotcha: modifier resolver must run before any reader of C_PositionGlobal3D — audit pipeline order. Idle bob must re-push its vec3 modifier each tick (ticksRemaining_ decay); gizmo drag is one-shot per drag-step (fits modifier model naturally). Blocked by T-191 (vec3 modifier kind). Parent epic: #731.
  - **Links:**

- [ ] **script: Lua input & command bindings — declare commands and bind inputs from Lua** — design-then-implement; expose IRInput command registration + key/mouse/gamepad input-to-command/status binding to Lua so creations no longer need a C++ initCommands() block
  - **ID:** T-193
  - **Area:** engine/script, engine/input, engine/command
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Stack:** T-193..T-196 lua-game-foundation
  - **Acceptance:** (1) design note `docs/design/lua-input-commands.md` lands in PR 1 covering: how Lua-defined commands plug into the existing command dispatch loop, lifetime of Lua callables relative to the command registry, archetype-batched vs per-event dispatch, and how a Lua-defined command can be referenced by C++ pipeline composition; (2) PR 2 implements: a Lua API that (a) declares a new command with a tick body, (b) binds a key/mouse/gamepad input to that command and to a status (held/pressed/released); (3) the 12-command `initCommands()` block in `creations/demos/default/main_lua.cpp:107-200` can be replaced with a Lua equivalent and the demo behaves identically; (4) existing C++ command path (`template <> struct IRCommand::COMMAND_NAME`) keeps working with no behavior change; (5) fleet-build clean on linux-debug and macos-debug
  - **Issue:** (none)
  - **Notes:** Highest-leverage gap blocking pure-Lua interactive games. Today the command system is C++-templated — see `creations/demos/default/main_lua.cpp:107-200` for how commands are currently registered. No `IRInput::createCommand` or input-handler binding is exposed to Lua. Phase as design-then-implement: design PR first, implementation PR second. Codegen support is optional — runtime registration is sufficient for v1. Parent epic: lua-game-foundation (T-193..T-196).
  - **Links:**

- [ ] **Research: Lua physics bindings — enumerate physics surface area and propose Lua API** — research-then-design; survey existing collision / raycasting / voxel-intersection surface, propose which APIs become Lua-callable, and identify which prefab systems need Lua hooks
  - **ID:** T-194
  - **Area:** engine/script, engine/physics
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Stack:** T-193..T-196 lua-game-foundation
  - **Acceptance:** (1) design note `docs/design/lua-physics-bindings.md` lands enumerating current physics surface area (collision, raycasting, voxel intersection, anything else actually present in the engine); (2) note proposes which APIs should be Lua-callable with concrete signatures; (3) note identifies which prefab systems need Lua hooks; (4) deliverable is framed around what's actually in the engine, not a generic physics-engine wishlist; (5) implementation deferred to a follow-up task filed once the design lands
  - **Issue:** (none)
  - **Notes:** Today there are no Lua bindings for physics. Confirm what physics surface even exists in the engine (it may be limited — voxel collision, basic raycasting). Likely smaller scope than T-193. If the engine has minimal physics, this collapses to "bind raycast + voxel intersection from Lua." Parent epic: lua-game-foundation (T-193..T-196).
  - **Links:**

- [ ] **docs: update lua-creation-setup skill for codegen + Lua-defined components/systems** — refresh `.claude/skills/lua-creation-setup/SKILL.md` to cover the codegen path and the IRComponent.register / IRSystem.registerSystem APIs that the existing skill predates
  - **ID:** T-195
  - **Area:** docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Stack:** T-193..T-196 lua-game-foundation
  - **Acceptance:** (1) `.claude/skills/lua-creation-setup/SKILL.md` includes a section on the `irreden_lua_codegen()` CMake helper with a worked example; (2) section on defining components and systems entirely in Lua via `IRComponent.register()` / `IRSystem.registerSystem()`; (3) guidance on when to choose codegen vs runtime EVAL mode; (4) updated worked example based on `creations/demos/lua_pipeline_demo` and `creations/demos/lua_perf_grid`; (5) existing manual binding sections stay (still needed for math types and helper namespaces) but are flagged as optional once you're using codegen-defined components
  - **Issue:** (none)
  - **Notes:** Skill is currently out of date — it documents the old pattern (manual `lua_bindings.cpp` per creation) and never mentions codegen. Reference docs: `engine/script/CLAUDE.md` lines 116-488 cover the Lua-driven ECS surface and codegen tool. Parent epic: lua-game-foundation (T-193..T-196).
  - **Links:**

- [ ] **Research: Lua binding automation — codegen extension + shared default bindings header** — short research note recommending an approach for auto-emitting `bindLuaType<>` specializations for the 40+ existing C++ `*_lua.hpp` components, plus a shared `registerStandardBindings(luaScript)` for math types and enums
  - **ID:** T-196
  - **Area:** engine/script, cmake/lua_codegen
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Stack:** T-193..T-196 lua-game-foundation
  - **Acceptance:** (1) `docs/design/lua-binding-automation.md` lands answering: (a) Should we extend the existing codegen tool to emit `bindLuaType<>` specializations for *existing C++ components* (the 40+ `*_lua.hpp` files in `engine/prefabs/`)? (b) Should math types and enums move to a shared `engine/script/lua_bindings_default.hpp` that creations include via one `registerStandardBindings(luaScript)` call instead of being re-listed per demo? (c) For (a), recommend an approach: regex-based header parsing, sidecar `.lua_bind` schema files per component, libclang, or stay with hand-written specializations; (2) note must include a "do nothing" option with a real argument for it (sometimes 40 trivial files is fine); (3) recommendation is concrete enough that 2-3 follow-up implementation tasks can be filed against it
  - **Issue:** (none)
  - **Notes:** The engine has 40+ `*_lua.hpp` files in `engine/prefabs/irreden/*/components/` that all follow the same shape (list constructors, list each field name + member pointer twice). Math types (`Color`, `vec3`, `ivec3`, etc.) and enums (`IREasingFunction`, `MidiNote`) are hand-listed in *every* demo's `lua_bindings.cpp` — `creations/demos/default/lua_bindings.cpp:31-132` shows the shape. Moving these to a shared default-bindings header is the easiest win and probably the first sub-task to do regardless of how (a) lands. The existing codegen tool lives at `cmake/lua_codegen/main.cpp`; lines 615-646 show what it emits for Lua-defined components. Extending it to handle C++-side components is plausible but requires a way to enumerate fields. The C++26 `std::reflect` proposal is too far out; libclang at codegen time is heavy but reliable; regex is brittle but cheap. Deliverable is the research note + a concrete recommendation. Implementation tasks (which would likely fan into 2-3 follow-ups: shared default bindings, codegen extension prototype, migration of existing _lua.hpp files) get filed once the research lands. Parent epic: lua-game-foundation (T-193..T-196).
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-187** — render LOD Phase 1 — computeLodLevel + per-shape lodMin filter · Owner: claude/T-187-lod-phase-1 · PR: https://github.com/jakildev/IrredenEngine/pull/727
- [x] **T-181** — prefab/runtime: C_BindPoints + entity:bindPoint Lua API · Owner: claude/T-181-bind-points-runtime · PR: https://github.com/jakildev/IrredenEngine/pull/720
- [x] **T-186** — test: JsonSidecarWriter + NameTable round-trips · Owner: claude/T-186-json-sidecar-name-table-tests · PR: https://github.com/jakildev/IrredenEngine/pull/730
- [x] **T-185** — asset: small cleanups — ShapeRecord serialized annotation, dead stub, makeTag length assert, CLAUDE.md refresh · Owner: claude/T-185-asset-cleanups · PR: https://github.com/jakildev/IrredenEngine/pull/726
- [x] **T-188** — script: decouple IrredenEngineScripting from IrredenEngineRendering · Owner: claude/T-188-decouple-scripting-rendering · PR: https://github.com/jakildev/IrredenEngine/pull/723
- [x] **T-184** — asset: delete entire .txl family (raw-binary + .txl.json sidecar + nlohmann dep) · Owner: claude/T-184-delete-txl-family · PR: https://github.com/jakildev/IrredenEngine/pull/722
- [x] **T-182** — prefab: attach voxel_ref data as ECS components on Prefab.spawn · Owner: claude/T-182-prefab-voxel-attach · PR: https://github.com/jakildev/IrredenEngine/pull/718
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
