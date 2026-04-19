# engine/render/ — trixel render pipeline

The biggest and most performance-critical module. Owns the voxel → trixel →
framebuffer → screen pipeline, all GPU resources, the camera entity, and the
canvas registry.

## Entry point

`engine/render/include/irreden/ir_render.hpp` — exposes `IRRender::` free
functions. Creations and other modules should **only** include this header,
never internal render headers.

Key exposed surface:

- `getRenderManager()`, `getRenderingResourceManager()`.
- Resource CRUD: `createResource<T>()`, `getResource<T>()`,
  `destroyResource<T>()`, plus `createNamed<T>(name, ...)` and
  `getNamedResource<T>(name)`.
- Voxel pool: `allocateVoxels(count)`, `deallocateVoxels(span)`.
- Canvases: `createCanvas(name, size, ...)`, `getCanvas(name)`,
  `setActiveCanvas(name)`.
- Camera and viewport: `getCameraPosition2DIso()`, `getCameraZoom()`,
  `getViewport()`, `getOutputScaleFactor()`, `mousePosToWorldPos()`.
- Subdivision mode: `setSubdivisionMode(NONE|POSITION_ONLY|FULL)`,
  `setVoxelRenderSubdivisions(n)`.
- GUI state: `setGuiVisible`, `setGuiScale`, `setHoveredTrixelVisible`.

## Two managers

**`RenderManager`** (`render/render_manager.hpp`) — stateful, per-frame:

- Owns the main framebuffer, main canvas (voxel pool), background canvas,
  GUI canvas, camera entity, and the `RenderImpl` backend.
- Stores render mode, subdivisions, camera pos/zoom, viewport.
- Drives `beginFrame()` → per-pipeline systems → `presentFrame()`.
- Maps canvas names → entity ids (`m_canvasMap`).

**`RenderingResourceManager`** (`render/rendering_rm.hpp`) — generic GPU
resource pool. Type-indexed storage (`typeid<T>.name()`), id reuse queue,
named lookup. Holds shaders, buffers, textures, VAOs, etc.

## The pipeline, one frame

```
┌──────────────────────────────────────────────────────────────────┐
│  INPUT pipeline: CAMERA_MOUSE_PAN + input systems                │
├──────────────────────────────────────────────────────────────────┤
│  UPDATE pipeline: game logic, voxel mutation                     │
├──────────────────────────────────────────────────────────────────┤
│  RENDER pipeline (systems in order):                             │
│    VOXEL_TO_TRIXEL_STAGE_1                                       │
│      • upload voxel pos/col/ids to SSBOs                         │
│      • clear distance texture to kTrixelDistanceMaxDistance      │
│      • c_voxel_visibility_compact.glsl → visible index list      │
│      • c_voxel_to_trixel_stage_1.glsl  → distance writes         │
│    VOXEL_TO_TRIXEL_STAGE_2                                       │
│      • c_voxel_to_trixel_stage_2.glsl  → color + entity id       │
│    SHAPES_TO_TRIXEL / TEXT_TO_TRIXEL  (optional overlays)        │
│    COMPUTE_VOXEL_AO                                              │
│      • c_compute_voxel_ao.glsl → per-pixel AO factor             │
│    COMPUTE_SUN_SHADOW                                            │
│      • c_compute_sun_shadow.glsl → per-pixel directional shadow  │
│    LIGHTING_TO_TRIXEL                                            │
│      • c_lighting_to_trixel.glsl → modulates canvas colors       │
│        by (AO × sun-shadow)                                      │
│    TRIXEL_TO_TRIXEL  (compositing/post)                          │
│    TRIXEL_TO_FRAMEBUFFER                                         │
│      • v_/f_trixel_to_framebuffer.glsl                           │
│      • reads canvas color/dist/id textures → writes framebuffer  │
│    FRAMEBUFFER_TO_SCREEN                                         │
│      • v_/f_framebuffer_to_screen.glsl                           │
│      • + f_debug_overlay.glsl if enabled                         │
└──────────────────────────────────────────────────────────────────┘
```

Every system in that list is a normal prefab under
`engine/prefabs/irreden/render/systems/`. Each must have its name in
`SystemName` enum before the specialization will link.

## Key components (defined in prefabs/irreden/render)

- `C_TriangleCanvasTextures` — 3 GPU textures (RGBA8 color, R32I distance,
  RG32UI entity id). Created in ctor, destroyed in `onDestroy()`.
- `C_EntityCanvas` — wraps a canvas entity id.
- `C_TrixelFramebuffer` — main output framebuffer (color + depth).
- `C_VoxelPool` — chunked voxel storage; filled by `allocateVoxels()`.
- `C_ZoomLevel`, `C_Camera`, `C_CameraPosition2DIso` — camera entity.
- `C_FrameDataTrixelToFramebuffer` — per-frame UBO (MVP, hover coord,
  distance offset, ...).
- `C_TextSegment`, `C_TextStyle`, `C_GeometricShape` — overlay prefabs.
- `C_TrixelCanvasRenderBehavior` — flags for camera/zoom/hover hookups.

## Shaders

Location: `engine/render/src/shaders/` (GLSL) and
`engine/render/src/shaders/metal/` (Metal).

Naming prefixes:

- `c_` compute — all voxel→trixel stages, text rendering, shape rendering,
  visibility compaction.
- `v_` vertex / `f_` fragment — `trixel_to_framebuffer`,
  `framebuffer_to_screen`, `debug_overlay`.
- `ir_iso_common.glsl`, `ir_constants.glsl` — shared includes.

Shader file paths are stored in `render/shader_names.hpp`. Update that
header when you add or rename a shader.

## Backends

`render/opengl/` and `render/metal/` each implement the `RenderImpl` /
`RenderDevice` interfaces. `RenderManager` holds one via `unique_ptr`.
Platform selection is compile-time (`IR_GRAPHICS_OPENGL` / `_METAL`).

## Verifying render changes

Rendering bugs rarely show up in the type checker or the test suite. Any
PR that touches:

- `engine/render/src/shaders/` (GLSL or MSL)
- `engine/prefabs/irreden/render/systems/` (pipeline systems)
- anything affecting pipeline ordering, canvas textures, or the voxel pool

must run the **`render-debug-loop`** skill after the change and attach at
least one before/after screenshot pair to the PR body. The skill drives
any creation that supports `--auto-screenshot` (today: `shape_debug`;
reference implementation is `creations/demos/shape_debug/main.cpp`) and
carries topic-indexed diagnosis tables for trixel / SDF shapes, lighting
phases, and backend-parity symptoms.

For changes that touch only one graphics backend (GLSL without MSL
counterpart, or vice versa), follow up with the **`backend-parity`**
skill on the lagging-side host — the rule is in the top-level CLAUDE.md
under "Cross-platform parity". `render-debug-loop` captures the
evidence; `backend-parity` drives the port.

Exceptions: pure header-doc edits, string-literal fixes, and internal
refactors with provably no runtime effect can skip the loop. When in
doubt, run it — a missing screenshot pair is a fast reviewer-rejection.

## Gotchas

- **Hardcoded uniform-buffer bind points.** Indices like
  `kBufferIndex_FrameDataVoxelToCanvas = 7` appear in both C++ and GLSL. A
  mismatch is silent — wrong uniforms, no error.
- **Distance texture clear.** Cleared to `kTrixelDistanceMaxDistance`
  (65535, **not** INT32_MAX). Voxels and shapes both write smaller values
  via `imageAtomicMin`; the clear value acts as the "nothing here" background.
  The shape SDF helpers use a separate `kInvalidDepth = 0x7FFFFFFF` (INT32_MAX)
  constant to signal "ray missed" and skip writing — don't confuse the two.
  If a clear is skipped, stale depth causes flicker.
- **Persistent mapped buffers.** `HoveredEntityIdBuffer` is
  `PERSISTENT | COHERENT`. Reading it too early (before GPU write) returns
  garbage from the previous frame.
- **Dispatch limits.** `kMaxDispatchGroupsX = 1024` (≈1M voxels before
  hitting the second dispatch dimension). Very large voxel counts slow.
- **Render mode × subdivision × zoom.** `SMOOTH` mode multiplies positions
  by `subdivisions × zoom`. Changing any of these mid-frame is a perf
  cliff and can desync chunk visibility.
- **Canvas destruction mid-frame.** `C_TriangleCanvasTextures::onDestroy()`
  frees GPU textures. If a canvas entity is destroyed while a system still
  references the canvas id, the next frame draws to freed handles.
- **VAO lifetime.** `QuadVAO` is named-resource-registered once at init.
  Do not destroy it from scripts; every `*_to_framebuffer` system depends
  on it.
- **Canvas render order matters.** Multiple canvases write to the same
  framebuffer in registration order. Stage 2 must complete for a canvas
  before the next canvas reads from it.
- **GUI canvas scaling.** GUI canvas is sized `mainCanvasSize / guiScale`.
  Changing `guiScale` without resizing the GUI canvas entity breaks
  coordinate mapping.
