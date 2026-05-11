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
  entity holding `C_Sprite + C_PositionGlobal3D + C_PositionOffset3D`
  as a textured alpha-blended quad, sorted back-to-front and grouped
  by atlas (one `drawArraysInstanced` per atlas). World position is
  `global + offset` per the engine-wide rendered-position convention.
  Bypasses the trixel pipeline; runs after the main canvas's
  `FRAMEBUFFER_TO_SCREEN` tick. Empty-case fast-path means a creation
  can register the system unconditionally — zero sprites = zero draws.

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

## Trixel UI widget framework

`widgets.hpp` exposes `IRPrefab::Widget::make<kind>(...)` builders and
`wasClicked` / `sliderValue` / `checkboxState` readers for the five Phase 0
F-0.1 primitives: **panel, label, button, slider, checkbox**. Each builder
produces a single ECS entity that carries `C_Widget` (kind + size +
disabled flag), `C_GuiPosition`, optionally `C_WidgetState` +
`C_HitBox2DGui` (interactive kinds only), and a kind-specific data
component (`C_WidgetPanel` / `C_WidgetLabel` / `C_WidgetButton` /
`C_WidgetSlider` / `C_WidgetCheckbox`). Theme lives in
`widget_theme.hpp::defaultTheme()` — a single inline header-level
instance a creation mutates once at init.

**Pipeline wiring (required order):**

```
INPUT:  HITBOX_MOUSE_TEST_GUI → WIDGET_INPUT
        → WIDGET_APPLY_SLIDER → WIDGET_APPLY_CHECKBOX
RENDER: ... → TEXT_TO_TRIXEL → WIDGET_RENDER_PANEL
        → WIDGET_RENDER_LABEL → WIDGET_RENDER_BUTTON
        → WIDGET_RENDER_SLIDER → WIDGET_RENDER_CHECKBOX
        → TRIXEL_TO_FRAMEBUFFER → ...
```

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

**Adding the sixth + widgets** (LIST, DROPDOWN, RADIO, TEXT_INPUT,
SCROLL) follows the same recipe: a `C_Widget<Kind>` data component, a
`WIDGET_RENDER_<KIND>` system iterating `C_Widget + C_Widget<Kind> +
C_WidgetState + C_GuiPosition`, optionally a `WIDGET_APPLY_<KIND>`
follower if the kind needs to mutate its own state on interaction.

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
