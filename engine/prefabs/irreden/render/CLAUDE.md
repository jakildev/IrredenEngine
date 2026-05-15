# engine/prefabs/irreden/render/ — canvases, framebuffers, cameras, text

Prefab half of the render module: the components, systems, commands, and
entity builders that the trixel pipeline reads and writes. Engine-side
state and device drivers live in `engine/render/`; this directory defines
the ECS surface.

## Key components

- `C_TriangleCanvasTextures` — owns 3 GPU textures (color / distance /
  entity-id). **Created in ctor, destroyed in `onDestroy()`.**
- `C_TrixelCanvasRenderBehavior` — toggles: use camera pan/zoom, run
  subdivisions, hover detection, pixel offset, etc.
- `C_TrixelFramebuffer` — wraps a `Framebuffer` (color + depth). Also
  ctor-allocated, `onDestroy()`-freed.
- `C_Camera` — tag.
- `C_CameraPosition2DIso` — iso-space position.
- `C_ZoomLevel` — float zoom.
- `C_CameraYaw` — continuous Z-yaw (radians), normalized to `[-π, π)`. See
  `camera.hpp` for the cardinal/residual split API.
- `C_TextSegment` — UTF-8 string for text-to-trixel.
- `C_TextStyle` — font, size, color.
- `C_GeometricShape` — 2D overlay shape descriptor.
- `C_FrameDataTrixelToFramebuffer` — per-frame UBO (MVP, hover coord,
  distance offset).
- `C_Sprite` / `C_SpriteSheet` / `C_SpriteAnimation` — 2D screen-composite
  sprite + atlas metadata + per-instance playback state. Sprites bypass the
  trixel pipeline and draw at the `FRAMEBUFFER_TO_SCREEN` stage;
  `C_SpriteAnimation` tracks the active sub-animation, frame index, elapsed
  time, and loop mode for the `SPRITE_ANIMATION_ADVANCE` UPDATE-phase
  system to write `uvRect` back into `C_Sprite`. `C_SpriteSheet` owns the
  atlas GPU texture handle — **freed in `onDestroy()`**; callers must not
  call `destroyResource` manually. See
  [`docs/design/sprites.md`](../../../../docs/design/sprites.md) for the
  full data model, depth semantics, and cross-task scope.
- `C_GizmoHandle` — marker on editor gizmo entities, tagging handle kind
  (translate-arrow, rotate-ring, scale-stick / scale-center, joint /
  bind-point / IK marker) + axis. Carries the Phase 2 (T-164) baseline
  fields — `referenceParams_` (the unscaled shape descriptor params
  written at construction), `referenceLocalPos_` (the unscaled local
  position), and `isAnchor_` (true for single-entity markers whose own
  `C_Position3D` is the world-space anchor — false for child handles
  parented under a group root). Also carries the Phase 3 (T-165)
  interaction fields — `hover_` (stamped each frame by `GIZMO_HOVER`
  from the GPU entity-id readback), `baseColor_` (the unmodulated
  color captured at spawn — the hover system writes a brightened tint
  onto the sibling `C_ShapeDescriptor` while hovered and restores the
  base on the same frame the hover bit clears), and `anchorEntity_`
  (the entity whose `C_Position3D` `GIZMO_DRAG` mutates — by
  convention the gizmo group parent, so all axes of a multi-handle
  gizmo share one anchor; `kNullEntity` keeps the handle hoverable
  but disables drag). Visible geometry comes from a sibling
  `C_ShapeDescriptor` on the same entity (SDF primitive rendered by
  `SHAPES_TO_TRIXEL`); the marker carries no rendering state itself.
  Builders live in `IRPrefab::Gizmo::` (see "Exposing system public
  API" below).
- `C_VoxelSelection` / `C_VoxelSelectionHighlight` — editor selection
  state and the tag that marks the highlight entity. The picking system
  (`VOXEL_PICKING`) mutates the selection on left-click; the highlight
  carries `C_Position3D + C_ShapeDescriptor` so the picked-voxel marker
  renders through the normal SDF path. Created hidden by the editor;
  toggled via `SHAPE_FLAG_VISIBLE` when a hit/miss happens.
- `C_ActiveLodLevel` — singleton row written by `LOD_UPDATE` each frame
  with the camera-zoom-derived `IRRender::LodLevel`. Read by
  `SHAPES_TO_TRIXEL` at `beginTick` to filter `C_ShapeDescriptor`
  entities whose `lodMin_` is below the active tier (i.e. requested
  more detail than the camera is providing). Phase 1 of the LOD story;
  see `docs/design/lod-strategy.md`.

## Key systems (all RENDER pipeline)

- `VOXEL_TO_TRIXEL_STAGE_1` / `STAGE_2` — compute-shader voxel
  rasterization to the 3 canvas textures.
- `TRIXEL_TO_TRIXEL` — compositing between trixel textures.
- `TEXT_TO_TRIXEL` — glyph rasterization (cap: 8192 glyph commands per
  frame).
- `SHAPES_TO_TRIXEL` — overlay shape rasterization.
- `TRIXEL_TO_FRAMEBUFFER` — reads the 3 canvas textures, writes the
  framebuffer.
- `FRAMEBUFFER_TO_SCREEN` — final blit with camera pan/zoom.
- `SPRITE_TO_SCREEN` — optional screen-composite pass that draws every
  entity holding `C_Sprite + C_PositionGlobal3D` as a textured alpha-
  blended quad, sorted back-to-front and grouped by atlas (one
  `drawArraysInstanced` per atlas). World position is read directly
  from `C_PositionGlobal3D` (modifier-driven offsets have already been
  folded in by `APPLY_POSITION_OFFSET` earlier in the UPDATE
  pipeline). Bypasses the trixel pipeline; runs after the main
  canvas's `FRAMEBUFFER_TO_SCREEN` tick. Empty-case fast-path means a
  creation can register the system unconditionally — zero sprites =
  zero draws.
- `VOXEL_PICKING` — editor input driver. On left-click PRESSED, casts a
  ray through the cursor (via `IRPrefab::Picking::castVoxelRay` —
  composes the same screen→world inverse `IRRender::mouseWorldPos3DAtIsoDepth`
  uses) and walks SDF shapes to find the first hit. Writes
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
at `beginTick` and skips `C_ShapeDescriptor` rows whose `lodMin_` is
below the active tier — purely a CPU-side filter ahead of the yaw /
cull-bounds math, no GPU staging cost for culled shapes, no shader
change. `LodLevel` indexes go DOWN as detail goes UP (`LOD_0` =
highest detail at zoom ≥ 16, `LOD_4` = silhouette tier always
drawn), and a shape's default `lodMin_ = LOD_4` means an unmarked
shape renders at every zoom. Register before `GLOBAL_POSITION_3D` in
the UPDATE pipeline so the singleton is current by the time RENDER
ticks. Phase 1 only filters `SHAPES_TO_TRIXEL`; DENSE-mode voxel
LOD, rig LOD, and multi-tier `.vxs` composition are out of scope.

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
  `C_Position3D` (translate) or to per-anchor accumulators on the
  system (rotate / scale): TRANSLATE_ARROW projects cursor world
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
returned group root. Each emitted handle carries `C_PositionGlobal3D +
C_Position3D + C_ShapeDescriptor + C_GizmoHandle + C_Name`; geometry
comes from the SDF primitives in `IRMath::SDF::ShapeType`
(`CYLINDER`, `CONE`, `TORUS`, `SPHERE`, `BOX`) rendered by the existing
`SHAPES_TO_TRIXEL` pass — no new render stage is introduced.

```cpp
// Place a translate gizmo at the origin, then attach it to a selection
// target by re-parenting or by writing C_Position3D::pos_.
EntityId gizmo = IRPrefab::Gizmo::createTranslateGizmo();
IREntity::getComponent<C_Position3D>(gizmo).pos_ = anchor;
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
readers) for the ten F-0.1 primitives: **panel, label, button, slider,
checkbox, list, dropdown, radio, text input, scroll**. Each builder
produces a single ECS entity that carries `C_Widget` (kind + size +
disabled flag), `C_GuiPosition`, optionally `C_WidgetState` +
`C_HitBox2DGui` (interactive kinds only), and a kind-specific data
component (`C_WidgetPanel` / `C_WidgetLabel` / `C_WidgetButton` /
`C_WidgetSlider` / `C_WidgetCheckbox` / `C_WidgetList` /
`C_WidgetDropdown` / `C_WidgetRadio` / `C_WidgetTextInput` /
`C_WidgetScroll`). Theme lives in `widget_theme.hpp::defaultTheme()` —
a single inline header-level instance a creation mutates once at init.

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
        → WIDGET_RENDER_DROPDOWN
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
filter already includes the kind-specific component.

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

## Gotchas

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
- **`SystemName` enum registration.** Every render system name
  (`VOXEL_TO_TRIXEL_STAGE_1`, ...) must exist in
  `engine/system/include/irreden/ir_system_types.hpp` or the
  specialization won't link.
