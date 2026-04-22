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
- `C_TextSegment` — UTF-8 string for text-to-trixel.
- `C_TextStyle` — font, size, color.
- `C_GeometricShape` — 2D overlay shape descriptor.
- `C_FrameDataTrixelToFramebuffer` — per-frame UBO (MVP, hover coord,
  distance offset).

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
that internally performs the entity-lookup logic. Keeps an ergonomic
free-function shape without putting the feature into `IRRender::` or adding
fields to `RenderManager`.

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
