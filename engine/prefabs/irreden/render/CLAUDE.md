# engine/prefabs/irreden/render/ — canvases, framebuffers, cameras, text

Prefab half of the render module: the components, systems, commands, and
entity builders that the trixel pipeline reads and writes. Engine-side
state and device drivers live in `engine/render/`; this directory defines
the ECS surface.

## Key components

- `C_TriangleCanvasTextures` — owns 3 GPU textures (color / distance /
  entity-id) plus a Hi-Z max-depth mip chain over the distance texture
  (`hiZMips_`, #1294). **All created in ctor, destroyed in `onDestroy()`.**
  The color/distance/entity-id format triple is centralized in
  `detail::makeCanvas*Texture` factories in its header so other canvas
  components can't drift from it; `detail::makeHiZMipChain` builds the
  downsampled R32I levels (conceptual level 0 is the distance texture).
  `COMPUTE_DISTANCE_HIZ` downsample-maxes the chain each frame for the
  voxel occlusion cull (`docs/design/voxel-occlusion-culling.md`). The
  chunk-occlusion pre-pass (#1294 child 2/3) consumes it inside
  `VOXEL_TO_TRIXEL_STAGE_1` — HZB-testing each pool-chunk's iso AABB against
  last frame's chain and ANDing occluded chunks out of `ChunkVisibility`
  before the compact pass — but is gated off by default
  (`IRRender::setVoxelOcclusionCullEnabled`), so the chain stays produced-only
  in the default pipeline. When enabled it also self-disables for one frame
  after a discontinuous camera move (#1294 child 3/3): `VOXEL_TO_TRIXEL_STAGE_1`
  `beginTick` compares the camera iso to last frame's and, on a jump over half
  the visible viewport (cut/teleport/first frame), keeps every chunk that frame
  — last frame's Hi-Z is the cull's lag source and is stale across a cut. Output
  is bit-identical cull-on vs cull-off (fully-occluded voxels write nothing); the
  per-chunk realized payoff is weak vs the per-voxel ceiling (per-voxel follow-on
  tracked separately). Also carries `renderedSubdivisions_`, stamped each frame
  by `VOXEL_TO_TRIXEL_STAGE_1` with the sub this canvas actually rastered at
  (below the global effSub when the `subdivisionCap` fired); the detached
  composite reads it to rescale model-frame depth into shared framebuffer units
  (the world-placed depth fix, see "World-integration mechanics" below). 0 = no
  voxel raster this frame.
- `C_PerAxisTrixelCanvases` — three per-axis (X/Y/Z) trixel texture sets
  for smooth rotation (#1308; `docs/design/per-axis-trixel-canvas-rotation.md`).
  Same GPU-RAII pattern as `C_TriangleCanvasTextures` but allocated
  **lazily**. Bundled on **every** voxel-pool canvas by
  `Prefab<kVoxelPoolCanvas>` (default-constructed inert). A single once-per-frame
  gate in `VOXEL_TO_TRIXEL_STAGE_1::beginTick` stands the textures up only while
  there is rotation to smooth: `syncAllocationToCameraYaw()` allocates the
  **main canvas's** while the camera sits at a non-cardinal residual yaw, and
  frees them at the cardinal so a static scene is byte-identical (fast path).
  This machinery is now **camera-only**: the detached per-axis forward-scatter
  (P3a/P3b, #1463–#1475) was **retired in #1560**. A rotating **detached** entity
  no longer uses per-axis canvases at all — its SO(3) renders through the
  **re-voxelize** path (#1555–#1560), which rotates+rounds the entity's voxel
  CELLS into a private pool that rasterizes through the normal single-canvas
  cardinal path + blit. For the **main world canvas**, T2
  (#1309) routes each visible voxel face into its axis canvas with continuous
  (`pos3DtoPos2DIsoYawed`) center reposition + shared world depth; T3
  (#1310) composites the three by depth-tested forward scatter at the
  framebuffer; T4 (#1311) lights each per-axis canvas (AO + sun-shadow +
  light-volume + Lambert) at trixel resolution before the scatter, adding
  per-axis `ao_` / `sunShadow_` textures with the same rotation-only
  lifecycle (the world sun-shadow map + 128³ light volume stay shared).
  **Per-axis base-resolution encoding (#1458):** the per-axis store writes at
  BASE (world-unit) resolution — one cell per voxel face, regardless of the
  active subdivision factor. The fractional sub-cell offset (where the voxel
  lands within its world cell) is packed into bits `[9:2]` of the distance
  encoding so the forward scatter can sub-pixel-shift each face quad and
  recover smooth detail without overflowing the base-resolution canvas.
  The canvas IS still sized at base-resolution (not `world × subPerAxis`);
  the per-axis dispatch still uses the capped density from
  `IRPrefab::PerAxisCanvas::subdivisionDensity()` for how many work-groups
  to launch, but z-slices ≥ 1 return early — only the z=0 invocation writes.
  AO, sun-shadow, lighting, and scatter all decode `rawDepth` directly as
  world units (`rawDist >> 10` for the per-axis path).
- `C_TrixelCanvasRenderBehavior` — toggles: use camera pan/zoom, run
  subdivisions, hover detection, pixel offset, etc.
- `C_TrixelFramebuffer` — wraps a `Framebuffer` (color + depth). Also
  ctor-allocated, `onDestroy()`-freed.
- Camera rotation lives in `C_LocalTransform.rotation_` (the same SQT
  quaternion every entity uses), composed as `qZ(yaw) × qX(pitch)`.
  `camera.hpp` exposes `IRPrefab::Camera::` helpers for both halves;
  the GRID trixel rasterizer reads only Z-yaw (via the cardinal/residual
  split), while DETACHED canvases consume the full quaternion through
  `system_propagate_canvas_rotation`. Pitch is clamped to ±(π/2 − ε)
  to avoid gimbal lock. `C_CameraYaw` was retired in T-364.
- `C_Sprite` / `C_SpriteSheet` / `C_SpriteAnimation` — 2D screen-composite
  sprite + atlas metadata + per-instance playback state. Sprites bypass the
  trixel pipeline and draw at the `FRAMEBUFFER_TO_SCREEN` stage;
  `C_SpriteAnimation` tracks the active sub-animation, frame index, elapsed
  time, and loop mode for the `SPRITE_ANIMATION_ADVANCE` UPDATE-phase
  system to write `uvRect` back into `C_Sprite`. `C_SpriteSheet` owns the
  atlas GPU texture handle — **freed in `onDestroy()`**; callers must not
  call `destroyResource` manually. See
  [`docs/design/sprites.md`](../../../../docs/design/sprites.md) for the
  full data model, depth semantics, and cross-task scope. The
  `C_Sprite::screenPixelSmooth_` flag opts a sprite out of the default
  game-pixel snap (it lands at floating-point screen precision instead);
  reserve for the player avatar or a camera-locked entity that should
  move between game pixels. See
  [`IRMath::cameraSubPixelOffsets`](../../../math/include/irreden/ir_math.hpp)
  for the granularity hierarchy the flag participates in.
- `C_GizmoHandle` — marker on editor gizmo entities, tagging handle kind
  (translate-arrow, rotate-ring, scale-stick / scale-center, joint /
  bind-point / IK marker) + axis. Visible geometry comes from a sibling
  `C_ShapeDescriptor` on the same entity (SDF primitive rendered by
  `SHAPES_TO_TRIXEL`); the marker carries no rendering state itself.
  Builders live in `IRPrefab::Gizmo::` (see "Exposing system public
  API" below).
- `C_VoxelSelection` / `C_VoxelSelectionHighlight` — editor selection
  state and the tag that marks the highlight entity. The picking system
  (`VOXEL_PICKING`) mutates the selection on left-click; the highlight
  carries `C_LocalTransform + C_WorldTransform + C_ShapeDescriptor` so
  the picked-voxel marker renders through the normal SDF path. Created hidden by the editor;
  toggled via `SHAPE_FLAG_VISIBLE` when a hit/miss happens.
- `C_ActiveLodLevel` — singleton row written by `LOD_UPDATE` each frame
  with the camera-zoom-derived `IRRender::LodLevel`. Read by
  `SHAPES_TO_TRIXEL` at `beginTick` to filter `C_ShapeDescriptor`
  entities whose inclusive `[lodMax_ .. lodMin_]` band excludes the
  active tier (too-fine or too-coarse for the camera's zoom). Disjoint
  bands across co-located variants give exclusive swap-not-stack LOD
  (#1467). Phase 1 of the LOD story; see `docs/design/lod-strategy.md`.

## Key systems

- `UPDATE_VOXEL_POSITIONS_GPU` — GPU voxel-position prepass (#1396). Runs
  **before** `VOXEL_TO_TRIXEL_STAGE_1` and computes `world = modelToWorld *
  localPos` into the binding-5 position SSBO for voxel sets that opt into
  transform indirection (`C_VoxelSetNew::gpuTransformSlot_ != kVoxelTransformStatic`).
  Voxels keep the sentinel by default, so the prepass skips them and the
  CPU-direct `UPDATE_VOXEL_SET_CHILDREN` flush still owns their slots —
  scenes with no GPU-transformed sets are byte-identical and pay no
  dispatch. Per-frame upload is one `mat4` per dynamic set, not O(voxels).
  Shared substrate for per-entity SO(3) (#1272/#1299) and skeletal voxels
  (#605). The transform slot is bit-packed into the local-position `.w`
  lane (Metal has no free buffer index past 30).
- `UPDATE_JOINT_MATRICES` — per-frame skeletal joint skin-matrix upload (#605
  Phase 2.2 / #1603). For each `C_Skeleton` it writes every joint's
  `IRPrefab::Skeleton::skinMatrix` (`jointWorld × bindInverse`) into a
  contiguous block of the **same** binding-18 `EntityTransformBuffer` the
  prepass consumes — no second buffer (binding-21 is retired for the voxel
  path in Phase 2.4). Phase 2.3 (#1605, `seedVoxelBoneSlots`) sets a skinned
  voxel's `.w` to `slotBase + bone_id` so the existing
  `c_update_voxel_positions` prepass skins it with no new shader: when a
  skeleton's slot block is (re)allocated, the system auto-stamps the rig
  root's `C_VoxelSetNew` per-voxel pool transform indices (lazily acquiring
  the set's entity slot via `IRPrefab::VoxelTransform::acquireSlot`; bone ids
  outside the joint list fall back to that entity slot = rigid follow).
  Editors that re-paint `C_Voxel::bone_id_` without changing the joint list
  re-stamp via `IRPrefab::JointTransform::seedVoxelBoneSlots(rigRoot)`;
  `IRPrefab::JointTransform::slotBase(rigRoot)` exposes the block for
  downstream phases. The association is same-entity: the skeleton and the
  skinned voxel set live on the same rig-root entity (the DENSE
  `Prefab.spawn` shape). Iterates the `<C_Joint, C_WorldTransform>` archetype
  (world transform via dense iteration, no per-joint `getComponent`); the rest
  pose + target slot per joint are gathered once in `beginTick` from each
  skeleton's own `bindPose_`. **The 4096-slot binding-18 budget is partitioned**
  so the prepass's contiguous `[0, maxSlotUsed_]` re-upload can never clobber a
  joint slot: dynamic voxel-set slots grow up from 0 (capped at
  `kJointTransformSlotBase`); joint blocks are carved down from
  `kMaxGpuVoxelTransforms` (the reserved `kMaxGpuJointTransforms`-slot high
  region). Register **after** `PROPAGATE_TRANSFORM` and **before**
  `UPDATE_VOXEL_POSITIONS_GPU`; a creation that authors skeletons must register
  the prepass too (this system reuses its buffer).
- `VOXEL_TO_TRIXEL_STAGE_1` — compute-shader voxel rasterization to the
  3 canvas textures. Runs compact + stage-1 + stage-2 dispatches in one
  per-canvas tick (the former separate `VOXEL_TO_TRIXEL_STAGE_2` system
  clobbered the shared voxel SSBOs across multi-canvas scenes).
- `TRIXEL_TO_TRIXEL` — compositing between trixel textures.
- `TEXT_TO_TRIXEL` — glyph rasterization (cap: 8192 glyph commands per
  frame).
- `SHAPES_TO_TRIXEL` — overlay shape rasterization.
- `TRIXEL_TO_FRAMEBUFFER` — reads the 3 canvas textures, writes the
  framebuffer.
- `FRAMEBUFFER_TO_SCREEN` — final blit with camera pan/zoom.
- `SPRITE_TO_SCREEN` — optional screen-composite pass that draws every
  entity holding `C_Sprite + C_WorldTransform` as a textured alpha-
  blended quad, sorted back-to-front and grouped by atlas (one
  `drawArraysInstanced` per atlas). Reads world position from
  `C_WorldTransform.translation_`. Bypasses the trixel
  pipeline; runs after the main
  canvas's `FRAMEBUFFER_TO_SCREEN` tick. Empty-case fast-path means a
  creation can register the system unconditionally — zero sprites =
  zero draws.
- `VOXEL_PICKING` — editor input driver. On left-click PRESSED, casts a
  ray through the cursor (via `IRPrefab::Picking::castVoxelRay` —
  composes the same screen→world inverse `IRRender::mouseWorldPos3DAtIsoDepth`
  uses) and walks both visible `C_ShapeDescriptor` SDF shapes and
  active voxels of every `C_VoxelSetNew` to find the first hit.
  Voxel-set hits return the owning entity, the world-aligned voxel
  coordinate, and a face normal suitable for place-adjacent math
  (`hit.faceNormal_` — ±1 along the dominant axis of
  `hit.worldHitPos_ - voxelCenter`). Writes
  `C_VoxelSelection` on the highlight entity and toggles its
  `C_ShapeDescriptor::flags_` visibility. Register after the camera
  systems and before `VOXEL_TO_TRIXEL_STAGE_1` so the highlight
  rasterizes at the new voxel the same frame.

## Level-of-detail (UPDATE pipeline; Phase 1)

`LOD_UPDATE` is the UPDATE-pipeline driver for the artist-driven LOD
story (`docs/design/lod-strategy.md`). Each frame it snapshots
`IRRender::getCameraZoom()`, maps it through
`lod_utils.hpp::computeLodLevel()`, and writes the result into the
`C_ActiveLodLevel` singleton. `SHAPES_TO_TRIXEL` reads the singleton
at `beginTick` and draws each `C_ShapeDescriptor` row only when the
active tier falls inside its inclusive LOD band `[lodMax_ .. lodMin_]`
(`shouldSkipAtLod` in `lod_utils.hpp`) — purely a CPU-side filter ahead
of the yaw / cull-bounds math, no GPU staging cost for culled shapes,
no shader change. `LodLevel` indexes go DOWN as detail goes UP (`LOD_0`
= highest detail at zoom ≥ 16, `LOD_4` = silhouette tier always drawn);
the band defaults (`lodMin_ = LOD_4`, `lodMax_ = LOD_0`) span the whole
range, so an unmarked shape renders at every zoom — byte-identical to
the pre-band single-sided filter. Co-located variants authored with
**disjoint** bands render exclusively: exactly one per zoom, swapping
in place instead of stacking (the #1467 fix — additive co-location
z-fought and read as glitchy). Register before `PROPAGATE_TRANSFORM` in
the UPDATE pipeline so the singleton is current by the time RENDER
ticks. Filters `SHAPES_TO_TRIXEL` only; DENSE-mode voxel LOD, rig LOD,
and multi-tier `.vxs` composition are out of scope.

## Editor gizmo interaction (INPUT pipeline; F-0.5 Phase 3)

Two co-pipelined systems drive the gizmo state machine. Both iterate
`C_GizmoHandle` entities; the editor wires them in the **INPUT**
pipeline immediately after `INPUT_KEY_MOUSE`, before any UPDATE work,
so a click in the same frame can already see hover state and apply
drag math.

- `GIZMO_HOVER` — reads `IRRender::getEntityIdAtMouseTrixel()` once
  per frame (one-frame-lag GPU readback of the persistent-mapped
  `HoveredEntityIdBuffer` populated by `f_trixel_to_framebuffer`).
  Stamps `C_GizmoHandle::hover_` on the handle whose entity id matches,
  clears all others, and writes a brightened tint onto the sibling
  `C_ShapeDescriptor` for visual highlight (restored from
  `C_GizmoHandle::baseColor_` on the same frame the hover bit clears).
- `GIZMO_DRAG` — left-mouse state machine. On press over a hovered
  handle, captures the anchor's local position, the cursor's world
  point projected onto a fixed iso-depth plane through the anchor,
  and the cursor's canvas-iso angle around the anchor. Each frame the
  mouse is down, applies kind-specific math to the anchor's
  `C_LocalTransform` (translate → `translation_`; rotate →
  `rotation_`, #1610) or to per-anchor accumulators on the
  system (scale): TRANSLATE_ARROW projects cursor world
  delta onto the handle's unit axis; ROTATE_RING tracks the canvas-
  iso angle change with Shift snapping to 15° (π/12); SCALE_STICK
  projects onto the axis with reference distance `kScaleStickRefWorld`
  voxels; SCALE_CENTER reads diagonal cursor screen displacement with
  reference `kScaleCenterRefPixels` pixels. Drag releases when the
  mouse goes up. The "fixed iso-depth plane" anchored at press time
  is what keeps the gizmo from running away from the cursor as its
  iso depth changes mid-drag.

Anchor convention: `IRPrefab::Gizmo::spawnHandle` records the
*parent* passed into the builder as the handle's `anchorEntity_`.
For `createTranslateGizmo` / `createRotateGizmo` / `createScaleGizmo`
that's the group entity returned by the builder, so drag moves the
gizmo as a unit. For `createJointMarker` / `createIKMarker` called
with a real anchor parent, drag would move that parent — passing
`kNullEntity` (Phase 1's default in `voxel_editor/main.cpp`) makes
those handles still hoverable but no-op on drag.
`createTranslateGizmoForAnchor(anchor)` (#1604) and
`createRotateGizmoForAnchor(anchor)` (#1610) use the parent-as-anchor
routing deliberately: the handles are parented to an existing entity and
drag mutates that entity's own `C_LocalTransform` (translation vs
rotation by handle kind) — the per-joint placement and FK-posing handles
in the voxel editor. No group entity is created.

See `engine/render/CLAUDE.md` for the full pipeline diagram.

## Exposing system public API from the prefab layer

Feature systems in this directory may need a public API surface so creations
and Lua bindings can drive their behavior. Two patterns:

**Pattern A — direct component access.** Caller grabs the relevant entity via
an ECS query and reads or writes the component directly. Best when the API
surface is small and callers already hold the entity id.

```cpp
// Caller holds the canvas entity and writes the fog component directly.
auto &fog = IREntity::getComponent<C_CanvasFogOfWar>(canvasEntity);
fog.setState(worldX, worldY, kFogStateVisible);
```

**Pattern B — prefab-scoped free-function namespace.** A header in this
directory (e.g. `fog_of_war.hpp`) exposes a namespace such as `IRPrefab::Fog::`
(declared in the feature's own header; name it anything that doesn't collide
with `IRRender::`) that internally performs the entity-lookup logic. Keeps an
ergonomic free-function shape without putting the feature into `IRRender::` or
adding fields to `RenderManager`.

```cpp
// Header exposes a namespace; callers do not need to hold the entity.
namespace IRPrefab::Fog {
    void setCell(int worldX, int worldY, std::uint8_t state);
    void revealRadius(int cx, int cy, int radius);
    void clear();
}
```

**What not to do.** Do not add a feature setter/getter to `IRRender::` or a
backing field to `RenderManager`. See `engine/render/CLAUDE.md`
§"What belongs in engine/render/ vs engine/prefabs/irreden/render/" for the
full principle, the rule of thumb, and the list of existing violations being
cleaned up.

## Editor gizmo primitives

`gizmo.hpp` exposes `IRPrefab::Gizmo::` builders that spawn the editor's
transform handles (translate / rotate / scale) and marker primitives
(joint / bind-point / IK) as small groups of child entities under a
returned group root. Each emitted handle carries `C_LocalTransform +
C_WorldTransform + C_ShapeDescriptor + C_GizmoHandle + C_Name`; geometry
comes from the SDF primitives in `IRMath::SDF::ShapeType`
(`CYLINDER`, `CONE`, `TORUS`, `SPHERE`, `BOX`) rendered by the existing
`SHAPES_TO_TRIXEL` pass — no new render stage is introduced.

```cpp
// Place a translate gizmo at the origin, then attach it to a selection
// target by re-parenting or by writing C_LocalTransform::translation_.
EntityId gizmo = IRPrefab::Gizmo::createTranslateGizmo();
IREntity::getComponent<C_LocalTransform>(gizmo).translation_ = anchor;
```

Phase 2 (T-164) wires the runtime polish: the
`GIZMO_SCREEN_SPACE_SIZE` UPDATE-pipeline system scales each handle's
`C_ShapeDescriptor::params_` (and the child-handle local position)
inversely with camera zoom so handles render at constant pixel size
across the editor's zoom range. The same task adds the generic
`SHAPE_FLAG_XRAY_OCCLUDED` bit to `ShapeFlags` and the matching path in
`c_shapes_to_trixel.glsl` / `c_shapes_to_trixel.metal` — the shader
knows nothing about gizmos; it blends a flagged shape's color over the
existing canvas pixel at reduced alpha wherever it loses the depth
contest. The gizmo builders opt every spawned handle into the flag so
occluded handles still read as a faint silhouette. Any other prefab
that needs a "see through walls" overlay (selection highlight, debug
marker) can opt in the same way without touching engine/render/.
Phase 3 (T-165) wires hover detection + drag interaction via the
`GIZMO_HOVER` / `GIZMO_DRAG` INPUT-pipeline systems — see "Editor
gizmo interaction" above for the state machine, the press-locked
iso-depth plane convention, and the anchor-routing rules used by the
builders.

## Trixel UI widget framework

`widgets.hpp` exposes `IRPrefab::Widget::make<kind>(...)` builders and
`wasClicked` / `sliderValue` / `checkboxState` (plus the T-177 follow-up
readers) for the eleven primitives: **panel, label, button, slider,
checkbox, list, dropdown, radio, text input, scroll, color swatch**.
Each builder produces a single ECS entity that carries `C_Widget`
(kind + size + disabled flag), `C_GuiPosition`, optionally
`C_WidgetState` + `C_HitBox2DGui` (interactive kinds only), and a
kind-specific data component (`C_WidgetPanel` / `C_WidgetLabel` /
`C_WidgetButton` / `C_WidgetSlider` / `C_WidgetCheckbox` /
`C_WidgetList` / `C_WidgetDropdown` / `C_WidgetRadio` /
`C_WidgetTextInput` / `C_WidgetScroll` / `C_WidgetColorSwatch`).
Theme lives in `widget_theme.hpp::defaultTheme()` — a single inline
header-level instance a creation mutates once at init.

`C_WidgetColorSwatch` (T-211) is a single solid-color clickable cell
for palette panels and theme editors. Each swatch carries its own
RGBA so a grid of distinct colors needs no theme override; the
`selected_` bit thickens the border and switches to
`theme.borderFocused_` so the active palette pick reads at a glance.
Mutual exclusion across a swatch group is consumer-owned — the
widget framework does not bind color swatches to a group id the way
radios do.

**Pipeline wiring (required order):**

```
INPUT:  HITBOX_MOUSE_TEST_GUI → WIDGET_INPUT
        → WIDGET_APPLY_SLIDER → WIDGET_APPLY_CHECKBOX
        → WIDGET_APPLY_LIST → WIDGET_APPLY_DROPDOWN
        → WIDGET_APPLY_RADIO → WIDGET_APPLY_TEXT_INPUT
        → WIDGET_APPLY_SCROLL
RENDER: ... → TEXT_TO_TRIXEL → WIDGET_RENDER_PANEL
        → WIDGET_RENDER_LABEL → WIDGET_RENDER_BUTTON
        → WIDGET_RENDER_SLIDER → WIDGET_RENDER_CHECKBOX
        → WIDGET_RENDER_LIST → WIDGET_RENDER_RADIO
        → WIDGET_RENDER_TEXT_INPUT → WIDGET_RENDER_SCROLL
        → WIDGET_RENDER_COLOR_SWATCH → WIDGET_RENDER_DROPDOWN
        → TRIXEL_TO_FRAMEBUFFER → ...
```

Register `WIDGET_RENDER_DROPDOWN` **last** among the per-kind renderers
so its expanded item panel paints over any neighbor it overlaps.

`WIDGET_INPUT` reads `C_HitBox2DGui::hovered_` populated by
`HITBOX_MOUSE_TEST_GUI` and writes the generic state machine into
`C_WidgetState` (hovered / pressed / fireAction / dragValue). The two
per-kind apply followers exist so the input system itself never calls
`getComponent` on per-entity kind-specific data — slider value and
checkbox toggle land on their own dedicated systems whose archetype
filter already includes the kind-specific component. Its `endTick`
publishes the z-ordered topmost hovered widget (`topHoveredId_`) into the
optional `C_GuiHoverState` singleton (#1796); create one per world with
`IRPrefab::Widget::makeGuiHoverState()` and read it back with
`hoveredWidget()`. No singleton → the publish is a no-op, so non-test
creations pay nothing.

**Lua `onClick` dispatch (#1975).** `system_widget_lua_dispatch.hpp`
(`WIDGET_LUA_DISPATCH`) turns a widget's `C_WidgetState::fireAction_` pulse
into an error-trapped Lua `onClick`, keyed per widget `EntityId` on the
system's own state (mirrors `DISPATCH_LUA_OVERLAP`; SERIAL — LuaJIT is
single-threaded). A Lua-driven creation registers it as a prefab system and
places it in the INPUT pipeline **immediately after `WIDGET_INPUT`** (so
`fireAction_` is fresh). It is the optional dispatch half of the
`IRGui.makeButton(..., onClick)` Lua binding — full surface +
wiring contract in `engine/script/CLAUDE.md` §"Widget framework bindings".

**Headless GUI assertions (P3, #1796).** `gui_test_assertions.hpp`
exposes `IRPrefab::GuiTest::` — capture-frame assertions
(`hovers` / `clickFires` / `sliderValue` / `checkbox` / `picksVoxel`)
over the introspectable widget + picking state, each emitting one
machine-readable `GUI-ASSERT …` log line. Evaluation lives in the prefab
layer (it needs widget / picking access engine/video lacks); the
`engine/video` scripted-shot harness drives it through the type-erased
`IRVideo::GuiTestConfig::onAssertFrame_` callback, threading a
caller-owned `GuiTest::LatchState` (CLICK_FIRES latches the one-frame
`fireAction_` pulse across the settle window). Reference wiring:
`creations/editors/voxel_editor/main.cpp` (`editor_gui_assert` /
`editor_pick_voxel` shots).

`WIDGET_RENDER_*` is split per kind for the same reason. Each renders
its own backgrounds, borders, and label text onto the **GUI canvas**
via `trixel_rect.hpp::fillRect` (one batched `subImage2D` per rect) +
`trixel_text.hpp::renderText`. These systems run **after**
`TEXT_TO_TRIXEL` so the canvas clear in TEXT_TO_TRIXEL's `beginTick` has
already happened — widget pixels overpaint where they overlap with
unrelated GUI overlay text. Place widgets away from the perf-stats
overlay region (top-right by default) until the canvas-clear contract
gets extracted into its own system (follow-up).

**Distance bands** (`trixel_rect.hpp`):

- `kWidgetBackgroundDistance` — solid fill behind everything else.
- `kWidgetBorderDistance` — borders + slider thumb.
- `kWidgetLabelDistance` — checkbox fill (the closest layer; below
  TEXT_TO_TRIXEL glyphs at `kGuiTextDistance` from `trixel_text.hpp`).

Distances govern only the framebuffer composite, not the per-canvas
write order — within a single widget tick the system draws bg → border
→ label in painter order to overwrite consistently.

**T-177 follow-up widgets** (LIST, DROPDOWN, RADIO, TEXT_INPUT, SCROLL)
mirror the same recipe as the Phase 0 five: a `C_Widget<Kind>` data
component, a `WIDGET_RENDER_<KIND>` system iterating `C_Widget +
C_Widget<Kind> + C_WidgetState + C_GuiPosition`, and a
`WIDGET_APPLY_<KIND>` follower. Two kind-specific notes worth knowing
when extending or composing widgets:

- **List + dropdown hover-row routing.** `WIDGET_APPLY_LIST` and
  `WIDGET_APPLY_DROPDOWN` reuse `state.dragValue_` (otherwise only the
  slider writes it) to convey the cursor's row index to the render
  system. -1 means "cursor outside any row." `WIDGET_RENDER_LIST` /
  `WIDGET_RENDER_DROPDOWN` paint a hover band on the matching row.
- **Dropdown hitbox grows when open.** `WIDGET_APPLY_DROPDOWN`
  mutates the dropdown's `C_HitBox2DGui::size_` to cover the expanded
  panel so subsequent frames' `WIDGET_INPUT` hover routing keeps
  reaching it. The hitbox shrinks back when the dropdown closes.
- **Radio group exclusion runs in endTick.** `WIDGET_APPLY_RADIO` sets
  the fired radio in its per-entity tick, then walks every
  `C_WidgetRadio` in `endTick` to clear siblings with the same
  `groupId_`. Use `IRPrefab::Widget::makeRadio(..., groupId, value)`
  to assign a creation-scoped group id.
- **Text input ↔ keyboard focus.** `WIDGET_APPLY_TEXT_INPUT` only
  edits the buffer when `state.focused_` is set. Focus is owned by
  the shared `WIDGET_INPUT` Tab cycle / click-to-focus logic — the
  apply system inherits it, no kind-specific focus plumbing.
- **Scroll widgets are visual primitives.** `C_WidgetScroll` carries
  the track + thumb, not children. The owning creation positions its
  scrollable content by reading `scrollPos_` itself; it does not
  reparent into the scroll widget.

## Rotation modes: GRID re-rasterizes the world pool, DETACHED rides a private canvas

A rotating solid reaches the framebuffer through one of two `C_RotationMode`
paths. Since #1624 (see `docs/design/detached-canvas-depth-default.md`) both
participate in world depth **by default**; the differences are cost and how
far lighting integration goes:

- **`GRID`** (the default) — `REBUILD_GRID_VOXELS` re-rasterizes the entity's
  rotated voxels into the **shared world pool** every frame, so they write world
  `trixelDistances` at their true iso depth (`x+y+z`). Consequence: GRID solids
  **depth-sort against, cast shadows onto, and receive shadows from** world
  geometry — they are fully world-integrated. Cost: per-frame world
  re-rasterization + the documented round-to-cell aliasing (`voxel/CLAUDE.md`).

- **`DETACHED` / `DETACHED_REVOXELIZE`** — the rotated solid renders on a
  **private canvas** (camera-yaw-zeroed, at the camera origin) which
  `ENTITY_CANVAS_TO_FRAMEBUFFER` (`system_entity_canvas_to_framebuffer.hpp`)
  composites at the iso screen position, paying no per-frame world
  re-rasterization. **By default the canvas is WORLD-PLACED** (#1624): the
  composite adds the entity's world iso depth
  (`pos3DtoDistance(roundVec3HalfUp(translation))`) to the canvas's
  model-frame distances, so the solid depth-sorts against GRID solids, the
  floor, and other detached canvases on the shared `trixelDistances`
  convention. A `DETACHED_REVOXELIZE` solid additionally **receives and
  casts** world sun-shadow + light-volume at its world position (the P4b
  pipeline below); plain `DETACHED` (octahedral-snap forward-scatter)
  depth-sorts only — its per-face deform has no faithful world-pos recovery
  for lighting (and it is on the retirement path, #1589).
  The **opt-out** `C_EntityCanvas::screenLocked_` restores the fixed-depth
  **2D overlay** (`model` Z = 0, `distanceOffset_` = 0, no world receive/cast)
  — byte-identical to the pre-#1624 default (the epic #1553 decision-1 /
  #1582 Option B contract) — for genuine overlay cases: HUD props,
  billboards, floating showcases.

**Picking a mode:** need world-integrated rotation with exact cell aliasing
(or shadows cast by thin/concave detail) → `GRID`. Need cheap smooth SO(3)
rotation that still sorts/casts/receives in the world → `DETACHED_REVOXELIZE`
(the default world-placed behavior). Need a screen-locked HUD/billboard
overlay → any `DETACHED` mode with `screenLocked_ = true`.

**World-integration mechanics (P4b / #1576; default since #1624).** How a
world-placed re-voxelize solid integrates:

- **Prerequisite — lighting-archetype membership (default since #2322 D1).**
  A detached re-voxelize canvas only participates in world lighting (AO + sun
  + sky + the RECEIVE path below) if it carries `C_CanvasAOTexture` **and**
  `C_TrixelCanvasRenderBehavior` — these put it in the `COMPUTE_VOXEL_AO` +
  `LIGHTING_TO_TRIXEL` archetypes. `IRPrefab::EntityCanvas::createWithVoxelPool`
  (`entity_canvas.hpp`) — the detached-canvas chokepoint, not the shared
  `kVoxelPoolCanvas` builder that also builds the main canvas — attaches both
  by default whenever the canvas isn't screen-locked, so a bare world-placed
  spawn needs zero extra components at the call site. Pass
  `screenLocked = true` to skip both entirely for a genuine overlay (HUD prop,
  billboard, floating showcase); before the default flipped, a spawn site that
  forgot the pair silently composited **raw albedo** ("pasted-on"), regardless
  of `worldPlaced_` / `detachedWorldReceive_` being set correctly.
- **Depth (P4b-1):** the composite sets `distanceOffset_` to the entity's world
  iso depth, so the solid depth-sorts against world geometry on the shared GRID
  `trixelDistances` convention. The shared framebuffer depth runs at
  `worldDepth × effSub × 4` (the `encodeDepthWithFace` ×4 face-bit shift over
  the ×subdivision-scaled depth), and a detached canvas rasters its pool at its
  OWN possibly-capped sub (`subdivisionCap`, #1570 D2), so the composite must
  (a) scale the offset to those units —
  `pos3DtoDistance(roundVec3HalfUp(translation)) × effSub × 4` — and (b) rescale
  the canvas's model-frame `rawDist` by `effSub / renderedSubdivisions_`
  (carried in `effectiveSubdivisionsForHover_.y`, applied in
  `f_trixel_to_framebuffer`). Leaving the offset in raw world units (the #1624
  default) under-scaled it by `effSub × 4`, sinking world-placed solids behind
  the floor as zoom grew. `screenLocked_` keeps the offset at 0 + scale 1
  (overlay, byte-identical).
- **Receive (P4b-2):** the entity world cell origin is propagated
  onto `C_CanvasLocalRotation` (`worldPlaced_` + `worldCellOffset_`) by
  PROPAGATE_CANVAS_ROTATION and published into the shared voxel-frame UBO as
  `detachedWorldReceive_`. When set, `LIGHTING_TO_TRIXEL` recovers each detached
  voxel's WORLD pos (model pos + the offset) and samples the SHARED world
  sun-shadow map (re-running the cascade lookup — shared `ir_sun_shadow_sample`
  with COMPUTE_SUN_SHADOW) + 128³ light volume there, so the solid darkens in
  world shadow and picks up light bleed like a GRID solid. Only the re-voxelize
  frame-data branch publishes the flag, so forward-scatter canvases never
  world-receive.
- **Cast (P4b-3, #1596):** `BAKE_SUN_SHADOW_MAP` scatters every world-placed
  re-voxelize canvas's model-frame distances (+ its world cell origin) into one
  shared main-canvas-layout scratch (`c_resolve_world_placed_depth`), blits to a
  bake-owned R32I resolve texture, and bakes that through the unchanged cardinal
  recovery — ONE extra bake regardless of caster count, mirroring the per-axis
  resolve precedent. Invariant: the sun-shadow bake only ever reads
  main-canvas-layout depth sources; a foreign model-frame canvas texture is
  never a bake input (the direct read returns empty through Metal's
  image-atomic scratch indirection — #1640 tracks the backend gap).

Match the GRID `trixelDistances` convention so a world-placed cell and the same
GRID cell composite to the same depth (the `detached_world_depth_test`
equivalence), and keep the `screenLocked_` overlay path byte-identical to the
pre-#1624 default.

**Density compensation (#2043).** The composite must mirror the main world
canvas's density compensation: **apparent on-screen size and the de-tile gather
sampling are functions of camera zoom × world extent only — never of the
internal raster resolution** (`cubeSub` / `renderedSubdivisions_`). The main
canvas enforces this with `canvasZoomLevel_ = cameraZoom / effSub`
(`system_trixel_to_framebuffer.hpp`);
`ENTITY_CANVAS_TO_FRAMEBUFFER` divides `cubeSub` out of the quad scale (via
`densityZoom = cameraZoom / cubeSub`) and scales the gather parity anchor up by
`cubeSub` the same way (`cameraTrixelOffset_ *= cubeSub`). Without it, a
generously-sized world-placed detached solid that the #1570-D2 footprint cap
admits at `cubeSub > 1` renders oversized (the `cubeSub` factor leaks into
on-screen extent) and gappy (the minified `cubeSub`-density tiles alias against
the NEAREST 1:1-assuming parity reconstruction). Every term is `× cubeSub` /
`/ cubeSub`, so `cubeSub ≤ 1` (tight canvas, cardinal, overlay) is byte-identical.
**Depth is independent** — it derives from `rawDist × depthScale`, not the quad
scale. Full invariant + verify steps:
[`docs/design/detached-canvas-density-compensation.md`](../../../../docs/design/detached-canvas-density-compensation.md).

## GPU stage timing (`gpu_stage_timing.hpp` / `gpu_stage_timing_observer.hpp` / `gpu_substage_timing.hpp`)

One GPU measurement per tagged `SystemId`, bracketing the whole tick via
device timestamp pairs (encoder-boundary counter samples on Metal). Some
registry rows are unwired and always read 0.000, and `shapePass1` is still a
whole-tick bundle — before quoting the overlay in a perf plan or attributing
cost to a sub-dispatch, read
[`docs/design/gpu-stage-timing-cost-model.md`](../../../../docs/design/gpu-stage-timing-cost-model.md)
(the reading contract + the measured dispatch cost model: empty early-return
sweeps are effectively free; shader-side early-return cannot reclaim
CPU-fixed dispatch-grid cost).

**Intra-tick sub-stage rows (#2280).** `VOXEL_TO_TRIXEL_STAGE_1` is NOT tagged
for the per-system observer. Its per-canvas tick brackets each dispatch group
with a `GpuSubStageScope` (`gpu_substage_timing.hpp`), so the `canvasClear` /
`voxelCompact` / `voxelStage1` / `voxelStage2` **GPU** rows are attributed
individually rather than bundled — `voxelStage1` now measures the stage-1
dispatch only, and the old whole-tick number is the sum of the four rows. A
sub-scope reuses the device timestamp machinery at the same attachment slot the
observer would have used (free because the system is untagged); on Metal the
distance-clear blit is captured via a timestamp-aware `createBlitEncoder`. The
**CPU** `voxelStage1` row stays the whole per-canvas tick (an
`IR_PROFILE_SCOPE` in the tick replaces the observer's CPU bracket) — GPU
sub-attribution is per-dispatch, CPU stays coarse. Sub-scopes are
single-canvas-exact (last-sample on multi-canvas) and do not cover the
rotating-only per-axis voxel dispatch.

## Gotchas

- **SQT transition complete (T-299/T-300/T-301a/T-302).** All render-side, update-side, and voxel-side readers/writers use `C_LocalTransform` + `C_WorldTransform`. The voxel pool's per-voxel SoA arrays are a dedicated 16-byte `IRRender::VoxelGpuPosition` POD (std430 stride contract). The legacy `C_Position3D` / `C_PositionGlobal3D` / `C_Rotation` components and `SYSTEM_GLOBAL_POSITION_3D` were deleted in T-302; consumers read `C_WorldTransform.translation_` and the SQT quat directly.
- **Canvas texture lifetime.** The 3 GPU textures owned by
  `C_TriangleCanvasTextures` are created in the ctor and only freed in
  `onDestroy()`. Destroying a canvas entity mid-frame while a system
  still holds a reference corrupts the next draw.
- **Framebuffer lifetime is symmetric.** Same pattern for
  `C_TrixelFramebuffer`. Don't construct one on the stack.
- **Parent-to-framebuffer.** If a canvas isn't explicitly parented,
  `entity_trixel_canvas.hpp` defaults it to the engine's main
  framebuffer. Mis-parented canvases render to the wrong surface.
- **Camera state is cached per frame.** Hitbox / hover systems cache
  `C_CameraPosition2DIso` + `C_ZoomLevel` at `beginTick`. If a system
  moves the camera mid-frame, hover tests may use stale values until
  the next frame.
- **Render-behavior flags are the knobs.** Changing pipeline behavior
  for a canvas (disable zoom tracking, turn off hover) is done via
  `C_TrixelCanvasRenderBehavior`, not by branching in the systems.
- **Detached screen-locking is on `C_EntityCanvas`, not the render behavior.**
  The `screenLocked_` opt-out (see "World-integration mechanics" above) lives on
  `C_EntityCanvas` — which `ENTITY_CANVAS_TO_FRAMEBUFFER` already iterates — not on
  `C_TrixelCanvasRenderBehavior` (the child canvas entity), so the composite reads
  it with no per-tick foreign getComponent.
- **Foreground depth priority is `C_EntityCanvas::depthPriority_` (#1958).** A
  world-placed canvas with `depthPriority_ != 0` composites into a RESERVED near
  depth band (`kDepthForegroundCeil`, `ir_render_types.hpp`) instead of its world
  iso-depth, so it renders unconditionally in front of the floor / any world
  geometry below it at every zoom and yaw — for floating showcases that must not
  clip behind the floor (the #1884 Bug-A fix; the two-tier disjoint near-plane
  partition in `f_trixel_to_framebuffer`). Only meaningful when `!screenLocked_`.
  World content (`depthPriority_ == 0`) is clamped OUT of the band — a no-op for
  in-budget content, so the cardinal fast path stays byte-identical. Source of
  truth: `docs/design/depth-unification-1884-investigation.md` §Resolution.
  Demo: `canvas_stress --only orbitswap` (far unit at entity-fg tier 1;
  `scripts/depth-tier-verify.py --only orbitswap --tier 1`, #2154).
- **Per-trixel priority tiers generalize the partition (#1960).** #1958's
  two-way split is now N disjoint foreground tiers (`kDepthForegroundTierCount`
  = 3: world / entity-fg / per-trixel-override) in `ir_render_types.hpp`;
  `f_trixel_to_framebuffer` resolves `tier = max(perEntityTier /*depthPriorityMode*/,
  perTrixelTier)` per fragment and pins `enc` into `depthForegroundTier{Lo,Hi}`.
  The per-trixel tier is authored per voxel (`C_VoxelSetNew::changeVoxelPriority`,
  low 2 bits of `C_Voxel::reserved_`) and **rides the top K=2 bits of the 64-bit
  entity id** in the `triangleEntityIds` channel — the single decode chokepoint
  (`decodeEntityId`/`decodePriority` in `ir_iso_common.{glsl,metal}`,
  `IRRender::decodeCarrierEntityId/Priority` in C++) masks the carrier off
  everywhere an id is READ (picking, hover) so a non-zero priority can't corrupt
  a picked id. Default priority 0 ⇒ byte-identical. **Caveat:** a per-trixel
  override only arbitrates ACROSS canvases at finalization — two voxel sets on
  ONE canvas resolve depth at the canvas raster (`atomicMin`), upstream of the
  partition, so an occluded same-canvas voxel's priority never reaches the
  composite (use separate detached units). A STATIC detached unit carries the
  carrier via the per-frame Voxel-record (binding 6) upload; a ROTATING
  `DETACHED_REVOXELIZE` unit carries it too since #2023 — the re-voxelize compute
  (`c_revoxelize_detached`) source grid is 3 uints/cell ({colorPacked,
  materialFlagBone, reserved}) and its MODE 1 inverse-resample dest write carries
  the `reserved` lane verbatim (stage 2 still masks `& 0x3u` at decode). Demo:
  `canvas_stress --only interpenetrate` (far unit rotated to exercise MODE 1).
- **Seed GPU-buffer sentinels GPU-side, never via a resource-sized CPU
  staging vector + `subData`.** For a repeating-single-byte sentinel (0x00,
  0xFFFFFFFF) use `IRRender::device()->fillBuffer(buffer, bytes, byteValue)`
  — GL `glClearNamedBufferSubData` / Metal blit `fillBuffer` under one
  primitive (the per-axis winner scratch in `dispatchPerAxisCanvases` is the
  reference use). Multi-byte sentinels like `kTrixelDistanceMaxDistance`
  (`0x0000FFFF`) have no driver-side clear — reuse an already-owned
  self-resetting kernel (e.g. one dispatch of the resolve blit) or a clear
  dispatch instead of a multi-MB cold-path staging alloc + upload.
  Live deviation to migrate when next touched:
  `system_resolve_per_axis_screen_depth.hpp` seeds its scratch the old way.
