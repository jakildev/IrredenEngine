# engine/prefabs/irreden/render/ — canvases, framebuffers, cameras, text

Prefab half of the render module: the components, systems, commands, and
entity builders that the trixel pipeline reads and writes. Engine-side
state and device drivers live in `engine/render/`; this directory defines
the ECS surface.

## Key components

- `component_triangle_canvas_textures.hpp` — `C_TriangleCanvasTextures`.
  Owns 3 GPU textures (color / distance / entity-id). **Created in
  ctor, destroyed in `onDestroy()`.**
- `component_trixel_canvas_render_behavior.hpp` —
  `C_TrixelCanvasRenderBehavior`. Toggles: use camera pan/zoom, run
  subdivisions, hover detection, pixel offset, etc.
- `component_trixel_framebuffer.hpp` — `C_TrixelFramebuffer`. Wraps a
  `Framebuffer` (color + depth). Also ctor-allocated, `onDestroy()`-freed.
- `component_camera.hpp` — `C_Camera`, tag.
- `component_camera_position_2d_iso.hpp` — iso-space position.
- `component_zoom_level.hpp` — `C_ZoomLevel`, float zoom.
- `component_text_segment.hpp` — UTF-8 string for text-to-trixel.
- `component_text_style.hpp` — font, size, color.
- `component_geometric_shape.hpp` — 2D overlay shape descriptor.
- `component_frame_data_trixel_to_framebuffer.hpp` — per-frame UBO
  (MVP, hover coord, distance offset).

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

## Commands

- `command_zoom_in.hpp`, `command_zoom_out.hpp`.
- `command_background_zoom_in.hpp`, `command_background_zoom_out.hpp`.
- `command_gui_zoom.hpp`.
- `command_move_camera.hpp`.
- `command_set_trixel_color.hpp`.
- `command_toggle_culling_freeze.hpp`.
- `command_toggle_gui.hpp`.

## Entity builders

- `entity_trixel_canvas.hpp` — bundles `C_SizeTriangles` +
  `C_TriangleCanvasTextures` + `C_Name` and parents to the main
  framebuffer by default.
- `entity_framebuffer.hpp` — creates a framebuffer entity.
- `entity_voxel_pool_canvas.hpp` — canvas that *also* has a
  `C_VoxelPool` (the common case).
- `entity_camera.hpp` — bundles `C_Camera`, `C_Position2DIso`,
  `C_Velocity2DIso`, `C_ZoomLevel`.

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
