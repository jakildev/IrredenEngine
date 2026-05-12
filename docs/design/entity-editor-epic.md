# Entity editor epic — canonical reference

**Status:** Plan landing. No phases implemented yet. Phase 0 implementation
tickets (F-0.1 — F-0.9) will be filed once this doc is on master.

**Umbrella:** [`#213`](https://github.com/jakildev/IrredenEngine/issues/213).
This doc is the long-form expansion of `#213` into 11 phases (`#603` —
`#613`). It is the engine-side source of truth; the game-side companion at
[`creations/game/irreden/docs/editor-needs.md`](https://github.com/jakildev/irreden/blob/master/irreden/docs/editor-needs.md)
(filed under `jakildev/irreden#60`) is the game-side source of truth for
what the editor must let authors create.

**Audience:** an Opus agent picking up any Phase 0 — Phase 10 implementation
ticket. Read this doc, then the phase-epic issue (`#603`–`#613`), then the
implementation ticket itself, in that order. Phase epics cross-reference
the matching section in this doc; this doc is the only file that holds the
full chain context.

---

## Table of contents

- [Why this exists](#why-this-exists)
- [Goals and non-goals](#goals-and-non-goals)
- [The big bet: trixel-rendered UI, no dear-imgui](#the-big-bet-trixel-rendered-ui-no-dear-imgui)
- [Architectural decisions (locked)](#architectural-decisions-locked)
- [Engine vs. game work split](#engine-vs-game-work-split)
- [Per-voxel record extension](#per-voxel-record-extension)
- [Phase 0 — Foundation (UI layer + scaffold)](#phase-0--foundation-ui-layer--scaffold)
- [Phase 1 — Static voxel authoring](#phase-1--static-voxel-authoring)
- [Phase 2 — Hierarchies & skeletal voxels](#phase-2--hierarchies--skeletal-voxels)
- [Phase 3 — Animation timeline + modular interpolation](#phase-3--animation-timeline--modular-interpolation)
- [Phase 4 — IK + chain solvers + dynamic adaptation](#phase-4--ik--chain-solvers--dynamic-adaptation)
- [Phase 5 — Bind-points & component attachment](#phase-5--bind-points--component-attachment)
- [Phase 6 — Procedural / variant authoring](#phase-6--procedural--variant-authoring)
- [Phase 7 — Particles, lights, audio bind-points](#phase-7--particles-lights-audio-bind-points)
- [Phase 8 — Multi-window / polish](#phase-8--multi-window--polish)
- [Phase 9 — Lua editor scripting](#phase-9--lua-editor-scripting)
- [Phase 10 — Voxel rep / perf review](#phase-10--voxel-rep--perf-review)
- [Cross-cutting concerns](#cross-cutting-concerns)
- [Open questions](#open-questions)
- [Cross-references](#cross-references)

---

## Why this exists

The original `#213` was a static voxel painter scoped to author `.txl`
files. During planning the scope expanded into a full **entity creation
mode** covering voxel authoring, bone hierarchies, IK with constraints,
keyframe + procedural animation, named bind-points, ECS component
attachment, and a trixel-rendered editor UI — because every one of those
capabilities is required to author the ~200–400 entities Irreden needs,
and authoring them outside the engine then porting back is strictly
worse than authoring them in the same runtime that draws them.

The expanded scope is large enough that:

1. The work has to be phased, not done in one shot. Each phase is a
   `fleet:epic` child issue (`#603`–`#613`) with its own sub-task list.
2. The dependency chain matters. Phases 0 — 5 are strictly sequential.
   Phases 6 — 10 fan out from Phase 5 in parallel.
3. Implementation tickets per phase fan out widely (~40 across the epic).
   Without a single canonical reference, each ticket would either repeat
   the phase scope inline or drift from the original plan.

This doc is the single canonical reference. It lives in-repo so changes
to scope/plan flow through normal review.

## Goals and non-goals

### Goals

- **Author 200 – 400 entity forms in-engine** — animals, plants, fungus,
  hybrids, parasites, special entities. Specifics in the
  [game-side editor-needs doc](https://github.com/jakildev/irreden/blob/master/irreden/docs/editor-needs.md).
- **Trixel-rendered editor UI** — no dear-imgui, no third-party UI libs.
  Reuses the existing trixel font + SDF/voxel rendering stack.
- **Skeletal animation + IK in-engine** — wire the existing
  `C_JointHierarchy` stub through to the GPU. FABRIK + Two-Bone IK.
  Runtime foot-IK and interaction-IK.
- **Modular interpolation** — registry of named C++ curves plus Lua-defined
  custom interpolators per keyframe.
- **Bind-point + component attachment** — named anchor points per rig that
  Lua scripts query at runtime. Game-defined components attach via UI;
  prefab format is Lua tables.

### Non-goals

- **Dear-imgui or any third-party UI lib.** Decided against; trixel UI
  is the bet.
- **Lua-defined components in the editor palette.** Lua-defined components
  are a separate engine epic. The editor's component palette is constrained
  to C++-defined components (Phase 5 scope).
- **Sparse voxel representation up front.** Phase 10 measures dense pool
  fragmentation under real workloads and decides whether sparse is needed.
  Premature optimization explicitly rejected.
- **Skinning in the renderer.** Per-voxel `bone_id` indexes a small GPU
  joint-matrix array; the existing voxel compute shader applies the
  transform. No mesh skinning, no per-vertex weights.

## The big bet: trixel-rendered UI, no dear-imgui

**This is the single biggest risk of the entire epic.** Phase 0 sub-task
F-0.1 (trixel UI primitives) is on the critical path — every later phase
that surfaces UI depends on this primitive set.

### Why trixel UI

- **No third-party dependency.** Dear-imgui is well-engineered but pulls
  in another rendering style, another input-handling model, and another
  resource lifecycle. Trixel UI reuses the engine's own primitives —
  trixel font, SDF rendering, isometric camera optional — and stays
  inside the engine's idioms.
- **Style consistency with the runtime.** The editor draws the same way
  the game draws. WYSIWYG for art previews. No "imgui aesthetic"
  intruding on screenshot reviews.
- **Self-contained for distribution.** When the editor is shipped to
  external authors, there's no UI lib to redistribute or version-pin.

### Why this is a risk

- **Scope of work.** A usable UI primitive set is 10+ widgets (panel,
  label, button, slider, list, dropdown, checkbox, radio, text input,
  scroll), plus layout (rows, columns, dock, splitters), plus input
  routing (hover, click, drag, keyboard focus, hotkeys). Each widget
  has hover/active/disabled states. Dear-imgui ships this in a known
  shape; trixel UI ships it for the first time.
- **No prior art to crib from.** The engine has trixel-font rendering
  (used by demos) and trixel rectangle/border helpers, but no widget
  framework. Phase 0 implementation tickets will design the input
  routing and focus model from scratch.
- **Slip risk dominates the epic timeline.** If F-0.1 slips, every
  later phase that needs UI (Phase 1 palette, Phase 3 timeline, Phase
  5 component palette) slips with it.

### Mitigations

- **Time-box Phase 0.** Per `#603` notes: if F-0.1 stalls, escalate
  immediately rather than expanding scope.
- **Phase 1 only needs five widgets minimum** (palette grid, button,
  toggle, slider, label). Implement those first; defer
  dropdown/scroll/text-input until a phase actually needs them.
- **Reuse trixel primitives where possible.** Borders are SDF rectangles
  with a stroke. Buttons are border + label + hover-color uniform. The
  trixel font already supports anti-aliased text at any size.
- **Fallback if trixel UI proves intractable.** Re-evaluate at Phase 0
  exit. The only acceptable fallback is "delay the epic", not "add
  dear-imgui" — dear-imgui's rendering pipeline conflicts with the
  trixel pipeline and the integration cost would itself be a
  multi-week epic.

## Architectural decisions (locked)

These were settled during the original `#213` expansion and are not
revisited per-phase:

| Decision | Rationale |
|---|---|
| **GUI rendered via trixel** | See above. No dear-imgui. No third-party UI lib. |
| **Skeletal animation + IK in-engine** | Wires existing `C_JointHierarchy`. FABRIK + Two-Bone. Foot-IK + interaction-IK at runtime. |
| **Modular interpolation** | Registry of built-in C++ curves; Lua-defined custom interpolators per keyframe. Zero-overhead for built-ins. |
| **Editor exe lives at `creations/editors/voxel_editor/`** | Gitignored — internal tool. |
| **Per-voxel record extends to 12 B** | `{material_id, flags, bone_id}` added. `bone_id = 0` is identity (zero-cost back-compat). |
| **Prefab format is Lua tables** | Consistent with the rest of the engine's Lua scripting story. Round-trips through `Prefab.spawn(...)`. |
| **Single-window invariant lifted only at Phase 8** | Real multi-window has cost; defer until single-window editor proves valuable first. |

## Engine vs. game work split

The work split is roughly **85 % engine, 15 % game** by code volume.

| Side | Scope |
|---|---|
| **Engine (~85 %)** | UI layer, editor app, all skeletal/IK/animation systems, `.txl` v2 extension, prefab format, voxel-rendering changes, multi-window, particle/light/audio bind-point components, Lua APIs. |
| **Game (~15 %)** | Actually authoring the ~200–400 entities; bind-point name conventions; the gameplay component pack the prefab UI exposes. |

The game-side companion doc enumerates the catalog, the bind-point
vocabulary, and the component pack. Engine-side phase work pulls schemas
and naming requirements from the game-side doc; the game-side doc updates
as game design evolves without re-touching the engine plan.

## Per-voxel record extension

The current `.txl` v1 per-voxel record is **4 bytes** — a packed color
index. The editor needs three additional fields, growing the record to
**12 bytes**:

| Field | Size | Purpose |
|---|---|---|
| `material_id` | 1 B | Index into a material registry (wood, chitin, flesh, scale, fungal tissue, …). Drives shader treatment (specularity, softness, translucency). |
| `flags` | 1 B | Bit-packed: `AO_CONTRIB`, `EMISSIVE`, `INTERACTIVE`, reserved bits. |
| `bone_id` | 1 B | Index into the per-entity bone array. `bone_id = 0` is identity (zero-cost compatibility with v1 scenes). |

Total padding bytes inside the 12 B record give room for additional
flags without re-versioning later.

### Back-compat

- **v1 → v2 load:** the version-aware loader (`#603` F-0.6) widens v1
  records to v2 with `material_id = 0`, `flags = AO_CONTRIB`,
  `bone_id = 0`. Identity bone keeps existing scenes rendering the
  same — no editor or game change required.
- **v2 → v1 save:** unsupported. Once an asset is upgraded, it stays
  v2. Editor warns on v1 open and writes v2 on save.

### GPU upload

The compute shader (`c_voxel_to_trixel_stage_1.glsl`) reads `bone_id`
to multiply by `bone_matrix[bone_id]` before placing the trixel.
`bone_matrix[0]` is the identity matrix maintained by the rig system —
no branching in the shader, no special-case path for unrigged voxels.

---

## Phase 0 — Foundation (UI layer + scaffold)

**Issue:** [`#603`](https://github.com/jakildev/IrredenEngine/issues/603).
**Blocked by:** none (root of the chain).

### Scope

Foundational primitives every later phase depends on: trixel-rendered UI
layer, 3D editor camera, gizmo primitives, per-voxel metadata extension,
`.txl` v2 + JSON sidecar, editor-app scaffold, voxel mouse picking.

### Sub-tasks

- **F-0.1** Trixel UI primitives (panel, label, button, slider, list,
  dropdown, checkbox, radio, text input, scroll).
- **F-0.2** Layout system (rows, columns, dock targets, splitters).
- **F-0.3** Input routing (mouse hover/click/drag, keyboard focus,
  hotkey table).
- **F-0.4** 3D editor camera (orbit + pan + zoom).
- **F-0.5** Gizmo primitives (translate/rotate/scale handles,
  joint/bind-point/IK markers).
- **F-0.6** Per-voxel metadata extension (`material_id`, `flags`,
  `bone_id`); `.txl` v2 with version-aware loader.
- **F-0.7** JSON sidecar format for `.txl` (human-diffable companion).
- **F-0.8** Editor exe scaffold at `creations/editors/voxel_editor/`.
- **F-0.9** Voxel mouse picking → world-space voxel selection.

### Acceptance

`fleet-build --target IRVoxelEditor` builds and runs. The editor boots,
draws an empty dockspace + 3D voxel viewport with grid, orbit camera
works, mouse picks a voxel under the cursor.

### Risks

- **F-0.1 trixel-UI slip.** Highest-risk single sub-task in the entire
  epic. See [the big bet](#the-big-bet-trixel-rendered-ui-no-dear-imgui)
  above. Time-box and escalate if it stalls.
- **F-0.6 `.txl` v2 back-compat.** Touching the on-disk format risks
  corrupting existing `.txl` assets if the loader's version detection
  is wrong. Land version-detection unit tests before the writer.
- **F-0.9 picking precision.** Voxel picking from screen ray needs to
  match the compute-shader trixel placement exactly, including bone
  transform once Phase 2 lands. Picking on the unrigged base mesh is
  acceptable for Phase 0.

---

## Phase 1 — Static voxel authoring

**Issue:** [`#604`](https://github.com/jakildev/IrredenEngine/issues/604).
**Blocked by:** Phase 0 (`#603`).

### Scope

A usable static voxel painter with symmetry, layers, frame-based animation,
and round-trip save/load including metadata. **Closes the original `#213`
acceptance criteria.** This phase is the deliverable that makes the
editor minimally useful even if no later phase ships.

### Sub-tasks

- **1.1** Place/erase, palette panel, undo stack.
- **1.2** Symmetry modes (X/Y/Z mirror, user-set plane offset).
- **1.3** Layer system (named voxel groups, visibility toggle).
- **1.4** Frame-based animation (multiple poses, scrubber).
- **1.5** Save/load round-trip with metadata + JSON sidecar.
- **1.6** First authored entities — ant (20³), simple bird, rock,
  mushroom, simple tree (validates the editor on real game targets).

### Acceptance

A 20³ voxel creature can be authored, saved as `.txl`, and loaded in
`IRShapeDebug`. Per-voxel metadata round-trips through `.txl` v2. JSON
sidecar is human-diffable.

### Risks

- **Undo-stack memory.** Per-voxel ops on a 64³ volume produce a lot of
  undo records. Cap stack depth or store deltas, not snapshots.
- **Symmetry edge cases.** Painting on the symmetry plane itself produces
  a single voxel, not two. Test the off-by-one before shipping.

---

## Phase 2 — Hierarchies & skeletal voxels

**Issue:** [`#605`](https://github.com/jakildev/IrredenEngine/issues/605).
**Blocked by:** Phase 1 (`#604`).

### Scope

Activate the existing `C_JointHierarchy` stub and the declared-but-unfilled
GPU joint-matrix buffer SSBO. Per-voxel `bone_id` is consumed by the
compute shader; multi-volume entities have each volume bound to a bone;
FK pose editing works in the editor.

### Sub-tasks

- **2.1** Drive `C_JointHierarchy` — fill GPU joint buffer SSBO each
  frame from CPU joint state.
- **2.2** Per-voxel `bone_id` consumed by compute shader stage 1
  (`c_voxel_to_trixel_stage_1.glsl` applies
  `bone_matrix[bone_id] * local_offset`).
- **2.3** Multi-volume entity, each volume bound to a bone.
- **2.4** Joint placement gizmos in editor (drag bones in 3D viewport).
- **2.5** Forward kinematics pose editing — rotate joints, see voxels
  deform live.
- **2.6** First skeletal entities — snake (15–40 segment chain),
  multi-limb creature, articulated tree.

### Acceptance

Rig a 30-segment snake; all segments deform when a joint matrix changes.
Existing v1 `.txl` files continue to render via identity-bone fallback
(`bone_id = 0`).

### Risks

- **GPU joint-buffer sizing.** The SSBO size cap matters once entities
  scale past trivial counts; design the slot allocation up-front.
- **Editor gizmo precision.** Bones are placed in world space but rotated
  in local space. Gizmo math has to round-trip through the parent chain
  cleanly; trust nothing without a unit test on a 3-bone chain.

---

## Phase 3 — Animation timeline + modular interpolation

**Issue:** [`#606`](https://github.com/jakildev/IrredenEngine/issues/606).
**Blocked by:** Phase 2 (`#605`).

### Scope

Keyframe authoring with modular interpolation — a registry of named
built-in curves plus Lua-defined custom interpolators selectable per
keyframe. Procedural animation modifiers and per-frame event triggers.

### Sub-tasks

- **3.1** `C_SkeletalAnimation` keyframe storage component.
- **3.2** Timeline UI panel (built on Phase 0 UI primitives).
- **3.3** Built-in interpolators — linear, smoothstep, hermite, bezier,
  step.
- **3.4** Interpolator registry — named functions, Lua-extensible.
- **3.5** Per-keyframe interpolator selection in timeline UI.
- **3.6** Procedural animation modifiers — sway, breathing, oscillate.
- **3.7** Per-frame event triggers — sound, particle, gameplay hook.

### Acceptance

Keyframe a walk cycle, scrub the timeline, see the rig animate with
selectable interp curves. A per-keyframe Lua interpolator runs inline.

### Risks

- **Lua interpolator perf.** Called per joint per frame — easy to land
  a 10× regression vs. the C++ path. Built-ins must stay in C++ (zero
  overhead); Lua interpolators are opt-in only for designer-authored
  special curves. Profile against the perf-grid baseline before shipping.
- **Event-trigger ordering.** Per-frame events fire at frame boundaries;
  if the frame is skipped (low FPS) or sampled mid-blend (high FPS),
  the event-firing rule needs to be deterministic. Settle the rule
  before the first event is wired.

---

## Phase 4 — IK + chain solvers + dynamic adaptation

**Issue:** [`#607`](https://github.com/jakildev/IrredenEngine/issues/607).
**Blocked by:** Phase 3 (`#606`).

### Scope

Two-Bone IK + FABRIK chain solver with constraints. Runtime adaptation
layers: foot-IK ground projection, interaction-IK target retargeting.
Animation blend stack composes authored keyframes + procedural overlay +
IK overlay.

### Sub-tasks

- **4.1** Two-Bone IK solver (limbs).
- **4.2** FABRIK chain solver (snakes, vines, multi-segment).
- **4.3** IK constraint types — rotation limit, hinge, ball, slider.
- **4.4** IK target authoring in editor.
- **4.5** Foot-IK ground adaptation — runtime layer raycasts down per
  foot, projects bone target onto terrain.
- **4.6** Interaction-IK — runtime hand-target retargeting when entity
  grabs/touches another entity.
- **4.7** Animation blend stack — authored + procedural + IK overlays.

### Acceptance

Walk cycle adapts to terrain — feet plant on ground regardless of
authored keyframe heights. Two creatures interact and IK adjusts the
relevant bones on both.

### Risks

- **Terrain raycast surface.** Engine has voxel pool but no documented
  terrain raycast utility. May surface as a Phase 4 implementation
  ticket — add it to engine API explicitly rather than ad-hoc inside
  the IK system.
- **Blend-stack order.** Authored / procedural / IK overlay must compose
  in a deterministic order; an authored keyframe overriding an IK target
  produces foot-through-floor visuals. Settle the order before 4.7
  lands.
- **Constraint-solver convergence.** FABRIK on long chains (30-segment
  snake) needs iteration bounds — early bailout vs. visible kink. Profile
  on the snake before declaring acceptance.

---

## Phase 5 — Bind-points & component attachment

**Issue:** [`#608`](https://github.com/jakildev/IrredenEngine/issues/608).
**Blocked by:** Phase 4 (`#607`).

### Scope

Make authored entities game-ready: scripts spawn at named bone anchors,
gameplay components attach via UI, prefabs save/load as Lua tables.
**This phase is the bottleneck for Phases 6 — 10**, which fan out from
here.

### Sub-tasks

- **5.1** `C_BindPoints` component — `unordered_map<string, {bone_id,
  offset, rotation}>`.
- **5.2** Bind-point gizmos in editor (place + name a marker).
- **5.3** Lua API — `entity:bindPoint("right_hand")` returns world-space
  transform.
- **5.4** Component palette UI — drag from registry, edit fields inline.
- **5.5** Prefab Lua format — entity template + component pack +
  voxel ref.
- **5.6** Prefab spawn API in C++ and Lua —
  `Prefab.spawn("<id>", position)`.

### Acceptance

Spawn a prefab from Lua at a world position;
`entity:bindPoint("<named_anchor>")` returns the correct world-space
transform. The component palette serializes attached components into the
prefab Lua and round-trips.

### Risks

- **Component-palette schema.** The palette reads the C++ component
  registry at startup. The registry's per-component field schema has
  to be machine-readable — extend `lua_component_pack.hpp` style or
  add a reflection layer. Lua-defined components are deliberately
  out of scope (separate engine epic).
- **Bind-point string-key cost.** `unordered_map<string, ...>` lookups
  per-frame per-entity are expensive if scripts query them in hot
  loops. Document the contract: bind-point lookup is a one-time query
  at spawn or interaction, not per-tick. If a per-tick use case appears,
  introduce an integer-handle API alongside the string API.
- **Prefab format stability.** Once prefabs ship, format breakage is
  expensive. Version the Lua format from day one (`prefab_version = 1`
  at the top).

---

## Phase 6 — Procedural / variant authoring

**Issue:** [`#609`](https://github.com/jakildev/IrredenEngine/issues/609).
**Blocked by:** Phase 5 (`#608`).

### Scope

Procedurally-grown entities (vines, hyphae), multi-stage lifecycles
(young/mature/dead), variant systems (recolor/rescale/swap-parts), and
runtime overlay layers (corruption, parasite veins) over a base
authored mesh.

### Sub-tasks

- **6.1** Growth-axis annotation per voxel set.
- **6.2** Procedural growth system — vines extending, hyphae branching.
- **6.3** Multi-stage entities — per-stage voxel sets, swap on stage
  transition.
- **6.4** Variant system — recolor / rescale / swap-parts on a base
  entity.
- **6.5** Layer overlays — runtime overlay over base authored mesh.

### Acceptance

A vine grows from a seed entity over 10 s; an overlay layer applies on
top of an authored base mesh without re-authoring it.

### Risks

- **Nightmare-overlay strategy unsettled.** Authored separate voxel
  set per nightmare variant vs. runtime shader pass over the base
  entity. Decide before 6.4 (see
  [open questions](#open-questions)).
- **Stage-transition cost.** Multi-stage entities swap voxel sets,
  which can mean a GPU upload of the full new set. For dense
  population (e.g. tree-of-life sequences), batch the upload window.

---

## Phase 7 — Particles, lights, audio bind-points

**Issue:** [`#610`](https://github.com/jakildev/IrredenEngine/issues/610).
**Blocked by:** Phase 5 (`#608`).

### Scope

Non-voxel attachments per bind-point: particle emitters, light sources,
spatial audio sources. **Engine-level lift to bring audio out of MIDI-only
into 3D positional audio.**

### Sub-tasks

- **7.1** `C_ParticleEmitter` per bind-point.
- **7.2** `C_LightSource` per bind-point.
- **7.3** Spatial audio source per bind-point — mixer / 3D attenuation /
  OpenAL or SoLoud or miniaudio integration choice surfaces here.
- **7.4** Per-frame audio event triggers — footstep on frame N,
  vocalization on frame M.

### Acceptance

Authored animation triggers a sound from the correct bone on the correct
frame; emitter particles spawn from the correct bind-point.

### Risks

- **Spatial-audio lib choice (7.3).** Meaningful engine-level decision.
  OpenAL is the legacy default; SoLoud is C++ and permissive; miniaudio
  is single-header. May spin off as its own epic if the integration
  becomes a larger thread.
- **Per-bind-point cost at scale.** 200 entities × 3 bind-points × per-tick
  attenuation update is non-trivial. Audio mixer needs early profile
  on the perf-grid scene density.

---

## Phase 8 — Multi-window / polish

**Issue:** [`#611`](https://github.com/jakildev/IrredenEngine/issues/611).
**Blocked by:** Phase 5 (`#608`).

### Scope

Real OS multi-window support; saved layouts; hotkey customization.
**Lifts the long-standing single-window invariant noted in
`engine/window/CLAUDE.md`.**

### Sub-tasks

- **8.1** Engine: lift single-window invariant — re-route GLFW
  callbacks per-window.
- **8.2** Multi-viewport floating panels.
- **8.3** Dock layout presets (paint mode, animate mode, prefab mode).
- **8.4** Layout save/load.
- **8.5** Hotkey customization.

### Acceptance

Pop a panel out into its own OS window; drag to a second monitor; layout
persists across editor restarts.

### Risks

- **Single-window invariant has fingerprints across engine.** The window
  module assumes one window today; lifting touches GLFW callback
  registration, input routing, the canvas-per-window assumption in
  render, and possibly per-window pipeline state. Audit before
  committing to 8.1 scope.
- **Defer until after a working editor.** Phase 8 is intentionally
  Phase-5-blocked rather than Phase-7-blocked so we have real
  single-window users before paying the multi-window cost.

---

## Phase 9 — Lua editor scripting

**Issue:** [`#612`](https://github.com/jakildev/IrredenEngine/issues/612).
**Blocked by:** Phase 5 (`#608`).

### Scope

Editor extensible by Lua scripts (Defold-style): voxel volume API, rig
API, custom command/menu hooks, and shipped helper scripts.

### Sub-tasks

- **9.1** Voxel volume Lua API (place / erase / query / bake).
- **9.2** Rig Lua API (bone, animation, IK target, bind-point).
- **9.3** Editor command hooks — register custom menus / tools from Lua.
- **9.4** Auto-rig and auto-symmetry-flip helpers as
  `creations/editors/voxel_editor/scripts/`.

### Acceptance

A "flip rig left/right" Lua tool registered from a script appears in
the editor menu and works on the active entity.

### Risks

- **Lua hot-reload during edit.** Builds on the engine's existing
  `replaceSystemBody` hot-reload mechanism for rapid iteration. Need
  to confirm the editor's command-hook table can be re-registered
  cleanly without leaking old hooks.
- **API surface explosion.** Voxel volume + rig + command-hook + Lua
  interpolator API + bind-point API together form a wide surface that
  changes the engine's public API significance. Stage carefully and
  treat each sub-API as its own design.

---

## Phase 10 — Voxel rep / perf review

**Issue:** [`#613`](https://github.com/jakildev/IrredenEngine/issues/613).
**Blocked by:** Phase 5 (`#608`).

### Scope

After the system has been stressed with real authoring, decide whether
the dense voxel pool is sufficient or whether a sparse representation
(octree / brick pool) is needed. **Measure, decide, and act — explicitly
no premature optimization.**

### Sub-tasks

- **10.1** Pool fragmentation under heavy edit (measure).
- **10.2** Decision: keep dense pool vs. sparse octree / brick pool.
- **10.3** Save-format compression.
- **10.4** GPU upload optimization for animated rigged voxels — skip
  position uploads when only matrices changed; bone-matrix-only updates
  per frame.

### Acceptance

Profile a 200-entity scene with full rigging; either keep the dense pool
with a profile in hand, or design a sparse alternative with a clear
migration plan.

### Risks

- **Sparse rewrite is its own multi-PR epic.** If 10.2 picks sparse,
  the migration is large and ripples through every voxel system. Plan
  10.2 as a decision deliverable, not a "implement sparse" deliverable.
- **Measurement load-bearing.** The decision hinges on the profile. The
  scene used has to reflect real gameplay density, not synthetic
  worst-case — measure against the actual game-side wave scene once
  authoring has produced it.

---

## Cross-cutting concerns

Themes that touch multiple phases and want a single home rather than
re-explanation per phase.

### Game-side authoring catalog

Specifics live in the
[game-side editor-needs doc](https://github.com/jakildev/irreden/blob/master/irreden/docs/editor-needs.md).
Engine implementation tickets pull bind-point naming, component pack
schemas, and animation-needs-by-category from there.

### Per-biome palettes, material slots, season recolor

Cross-cutting authoring needs the game side has explicitly called out:

- **Per-biome color palettes.** Each biome has a dominant color range.
  Entities authored "for biome X" inherit that palette. Editor lets an
  entity declare its biome palette and override individual voxels.
- **Material slots.** `material_id` indexes a material registry (wood,
  chitin, flesh, scale, fungal tissue, …). Drives shader treatment.
- **Season recolor.** Winter/spring/summer/fall color swaps for plants.
  Authored as a recolor variant of the base entity (Phase 6.4).

### Wave / spawning metadata

The game's wave system requires per-entity metadata: `wave_group`
(commons / special / head), `population_target`, taming flags. Lands as
a `C_Wave` component in the gameplay component pack (Phase 5).

### Animation needs by category

Pulled from the game-side doc — seven visual representation categories
each have different animation/IK requirements. Engine implementation
tickets target the categories rather than enumerating entities:

| Category | Animation type | IK needs |
|---|---|---|
| Static rigid | None | None |
| Frame-based | Pose-swap per frame | None |
| Skeletal keyframe | FK keyframes | Foot-IK; rotation limits per joint |
| Skeletal + dynamic IK | FK + runtime IK | Foot-IK + interaction-IK |
| Procedural growth | Time-parameterized growth | Growth-axis constraint solver |
| Soft-body | Sim-driven | None |
| Particle | Procedural drift | None |
| SDF aura | None or pulse | None |

### Coordination with the screen-space sun shadow path

The screen-space sun-shadow bake (`docs/design/screen-space-sun-shadow-map.md`)
reads the iso distance map and is largely orthogonal to the editor —
authored entities cast shadows the same way the existing game entities
do. However: Phase 2's per-voxel `bone_id` transforms voxel positions
in compute stage 1, which means the iso depth feeding the shadow bake
already reflects the rigged pose. No extra plumbing needed; document
this once Phase 2 lands so the next render-pipeline PR doesn't have
to re-derive it.

## Open questions

Surfaced during planning. To be resolved before the relevant phase
starts; tracked here rather than scattered across 11 issues.

- **Material registry size.** Wood, chitin, flesh, scale, fungal tissue,
  stone, water, fabric — that's 8. Add bioluminescent, oily, glassy,
  heat-shimmer — that's 12. Cap at 16? 32? `material_id` is 1 byte so
  256 is the hard ceiling. **Owner:** game architect. **Phase:** before
  F-0.6.
- **Nightmare-overlay strategy.** Authored separate voxel set per
  nightmare variant, or runtime shader pass over the base entity?
  Authored doubles the asset count; shader pass is one path but
  constrains the look. **Owner:** game architect. **Phase:** before 6.4.
- **Bind-point arity / naming convention.** Some entities (snake) want
  indexed bind-points (`segment_0`, `segment_1`, …). Others (ant) want
  fixed names (`right_hand`). Settle the naming convention before
  Phase 5 sub-task 5.1 commits the `unordered_map<string, ...>`
  contract. **Owner:** game architect. **Phase:** before 5.1.
- **Spatial-audio library choice.** OpenAL vs. SoLoud vs. miniaudio.
  Decide at 7.3 — if the integration becomes a larger thread, spin off
  as its own epic. **Owner:** engine architect. **Phase:** before 7.3.
- **Wave / spawn density target.** The 200-entity Phase 10 stress
  target — what fraction is voxel-rigged vs. lightweight (particle,
  SDF)? Drives whether the dense-pool decision is even relevant.
  **Owner:** game architect. **Phase:** before 10.1.

## Cross-references

- **Umbrella issue:** [`jakildev/IrredenEngine#213`](https://github.com/jakildev/IrredenEngine/issues/213)
- **Phase epics:** [`#603`](https://github.com/jakildev/IrredenEngine/issues/603)
  · [`#604`](https://github.com/jakildev/IrredenEngine/issues/604)
  · [`#605`](https://github.com/jakildev/IrredenEngine/issues/605)
  · [`#606`](https://github.com/jakildev/IrredenEngine/issues/606)
  · [`#607`](https://github.com/jakildev/IrredenEngine/issues/607)
  · [`#608`](https://github.com/jakildev/IrredenEngine/issues/608)
  · [`#609`](https://github.com/jakildev/IrredenEngine/issues/609)
  · [`#610`](https://github.com/jakildev/IrredenEngine/issues/610)
  · [`#611`](https://github.com/jakildev/IrredenEngine/issues/611)
  · [`#612`](https://github.com/jakildev/IrredenEngine/issues/612)
  · [`#613`](https://github.com/jakildev/IrredenEngine/issues/613)
- **Game-side companion doc:** `creations/game/irreden/docs/editor-needs.md`
  ([`jakildev/irreden#60`](https://github.com/jakildev/irreden/pull/60))
- **Related engine design docs:**
  [`docs/design/screen-space-sun-shadow-map.md`](screen-space-sun-shadow-map.md)
  (shadow path interacts with rigged voxels in Phase 2),
  [`docs/design/lua-driven-ecs.md`](lua-driven-ecs.md) (Lua scripting
  surface that Phase 9 extends).
