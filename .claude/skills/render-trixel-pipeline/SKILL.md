---
name: render-trixel-pipeline
description: >-
  Work with the Irreden Engine render pipeline: voxel-to-trixel stages, canvas
  textures, trixel compositing, framebuffer output, shaders, camera, and
  coordinate systems. Use when the user wants to modify rendering, add shader
  stages, work with canvases, adjust camera/viewport, or understand the
  isometric projection math.
---

# Render / Trixel Pipeline

## Pipeline Overview

```
Voxels (C_VoxelPool positions/colors)
    |
    v
Stage 1: Depth pass (c_voxel_to_trixel_stage_1.glsl)
    |    Writes distance buffer (per-trixel depth)
    v
Stage 2: Color pass (c_voxel_to_trixel_stage_2.glsl)
    |    Writes RGBA colors, distances, entity IDs
    v
Shapes to Trixel (c_shapes_to_trixel.glsl)  [optional]
    |    SDF-based shape rasterization
    v
Text to Trixel (c_text_to_trixel.glsl)  [optional]
    |    Glyph rendering via TrixelFont
    v
Trixel to Trixel (c_trixel_to_trixel.glsl)  [optional]
    |    Composites child canvases onto parent canvas
    v
Trixel to Framebuffer (v/f_trixel_to_framebuffer.glsl)
    |    Renders trixel canvas to screen-sized framebuffer
    v
Debug Overlay (v/f_debug_overlay.glsl)  [optional]
    v
Framebuffer to Screen (v/f_framebuffer_to_screen.glsl)
    |    Final blit to window
```

## System Registration Order

Systems must be registered in the RENDER pipeline in dependency order:

```cpp
IRSystem::registerPipeline(
    IRTime::Events::RENDER,
    {IRSystem::createSystem<IRSystem::CAMERA_MOUSE_PAN>(),
     IRSystem::createSystem<IRSystem::UPDATE_VOXEL_POSITIONS_GPU>(),
     IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
     IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_2>(),
     IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
     IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
     IRSystem::createSystem<IRSystem::TRIXEL_TO_TRIXEL>(),
     IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
     IRSystem::createSystem<IRSystem::DEBUG_OVERLAY>(),
     IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>()}
);
```

Not all systems are required. A minimal setup needs: `VOXEL_TO_TRIXEL_STAGE_1`, `VOXEL_TO_TRIXEL_STAGE_2`, `TRIXEL_TO_FRAMEBUFFER`, `FRAMEBUFFER_TO_SCREEN`.

## Coordinate Systems

### 3D Voxel Space

- **X axis** -- lower-left in isometric view
- **Y axis** -- lower-right in isometric view
- **Z axis** -- upward (vertical)

Ground plane is XY. Entities at the same Z sit on the same horizontal plane.

### 3D to Isometric 2D

```
iso.x = -x + y
iso.y = -x - y + 2z
```

Function: `IRMath::pos3DtoPos2DIso(ivec3 position)`.

### Isometric Depth

```
distance = x + y + z
```

Objects with the same `x+y+z` share a depth layer. Higher distance = further from camera. `IRMath::isoDepthShift(pos, d)` shifts by `(d,d,d)` -- changes depth without altering the 2D projection.

### Face Types

Each voxel exposes up to three visible faces:

| FaceType | Perpendicular to | Visible side | Trixels per face |
|----------|-------------------|--------------|-----------------|
| `Z_FACE` | Z axis | Top | 2 triangles |
| `X_FACE` | X axis | Right | 2 triangles |
| `Y_FACE` | Y axis | Left | 2 triangles |

Mapped in the compute shader via `localIDToFace()` with work group size `(2, 3, 1)`: 6 invocations per voxel, 2 trixels per face.

## Shaders

All shaders live in `engine/render/src/shaders/`. Naming convention:

| Prefix | Type | Example |
|--------|------|---------|
| `c_` | Compute | `c_voxel_to_trixel_stage_1.glsl` |
| `v_` | Vertex | `v_trixel_to_framebuffer.glsl` |
| `f_` | Fragment | `f_trixel_to_framebuffer.glsl` |
| `g_` | Geometry | (none currently) |

Metal equivalents exist under `engine/render/src/shaders/metal/`.

### Key Compute Shaders

**`c_voxel_to_trixel_stage_1.glsl`** -- Depth pass. Work group `(2,3,1)`. Reads position/color SSBOs and chunk visibility mask. Writes `r32i` distance buffer via `imageAtomicMin`. Each invocation handles one trixel of one face.

**`c_voxel_to_trixel_stage_2.glsl`** -- Color pass. Same work group. Reads distance buffer written by stage 1. Writes `rgba8` color, `r32i` distance, and `rg32ui` entity ID images. Only writes trixels whose distance matches the depth buffer (painter's algorithm resolved).

**`c_shapes_to_trixel.glsl`** -- SDF shape rasterization. Evaluates `GPUShapeDescriptor` shapes (box, sphere, cylinder, ellipsoid, wing, prism, tapered box, custom SDF) with joint transforms and LOD.

**`c_trixel_to_trixel.glsl`** -- Canvas compositing. Copies trixels from a child canvas onto a parent canvas with position offset and depth testing.

## GPU Data Structures

### Buffer Bindings (from `ir_render_types.hpp`)

| Binding | Name | Type |
|---------|------|------|
| 0 | `FrameDataUniform` | UBO |
| 5 | `SingleVoxelPositions` | SSBO (`vec4[]`) |
| 6 | `SingleVoxelColors` | SSBO (`uint[]`, packed RGBA) |
| 7 | `FrameDataVoxelToCanvas` | UBO |
| 10 | `FrameDataTrixelToTrixel` | UBO |
| 13 | `VoxelEntityIds` | SSBO (`uvec2[]`) |
| 20 | `ShapeDescriptors` | SSBO |
| 21 | `JointTransforms` | SSBO |
| 24 | `ChunkVisibility` | SSBO (`uint[]`, bitmask) |

### Image Bindings (trixel canvas)

| Binding | Format | Purpose |
|---------|--------|---------|
| 0 | `rgba8` | Trixel canvas colors |
| 1 | `r32i` | Trixel canvas distances |
| 2 | `rg32ui` | Trixel canvas entity IDs |

### FrameDataVoxelToCanvas (binding 7)

```cpp
struct FrameDataVoxelToCanvas {
    vec2 cameraTrixelOffset_;
    ivec2 trixelCanvasOffsetZ1_;
    ivec2 voxelRenderOptions_;   // x = SubdivisionMode, y = subdivisions
    ivec2 voxelDispatchGrid_;
    int voxelCount_;
    int _voxelDispatchPadding_;
    ivec2 canvasSizePixels_;
    ivec2 _canvasPadding_;
};
```

## Key Components

### C_VoxelPool

Allocates contiguous GPU-friendly storage for voxel positions, colors, and entity IDs. Partitioned into chunks of `kVoxelChunkSize = 256` for frustum culling.

**File:** `engine/prefabs/irreden/voxel/components/component_voxel_pool.hpp`

### C_TriangleCanvasTextures

Owns the trixel canvas GPU textures (color, distance, entity ID images). Each canvas entity has one. The voxel-to-trixel system renders into this canvas.

**File:** `engine/prefabs/irreden/render/components/component_triangle_canvas_textures.hpp`

### C_EntityCanvas (WIP)

Wraps a canvas entity for per-entity rendering. Creates its own `C_VoxelPool` + `C_TriangleCanvasTextures`.

### C_CanvasTarget (WIP)

Points an entity to a specific canvas entity for rendering (defaults to main canvas).

## Render Modes

| Mode | Enum | Effect |
|------|------|--------|
| None | `SubdivisionMode::NONE` (0) | Integer-aligned voxel positions (no subdivision) |
| Position Only | `SubdivisionMode::POSITION_ONLY` (1) | Subdivided positioning, base-resolution SDF eval |
| Full | `SubdivisionMode::FULL` (2) | Full subdivision: positions × subdivisions × zoom |

`voxelRenderOptions_.x` controls mode, `.y` controls subdivision level.

## Shape Types (for SHAPES_TO_TRIXEL)

| ShapeType | Value |
|-----------|-------|
| `BOX` | 0 |
| `SPHERE` | 1 |
| `CYLINDER` | 2 |
| `ELLIPSOID` | 3 |
| `WING` | 4 |
| `PRISM` | 5 |
| `TAPERED_BOX` | 6 |
| `CUSTOM_SDF` | 7 |

Shapes support flags: `HOLLOW`, `MIRROR_X`, `MIRROR_Y`, `VISIBLE`, `DISCRETE_ROTATION`.

## LOD Levels

```cpp
enum class LodLevel : uint32_t {
    LOD_0 = 0,  // full detail
    LOD_1 = 1,
    LOD_2 = 2,
    LOD_3 = 3,
    LOD_4 = 4   // most reduced
};
```

LOD utilities live in `engine/prefabs/irreden/render/lod_utils.hpp`.

## Trixel Compositing Pattern

`TRIXEL_TO_TRIXEL` uses `CHILD_OF` relations: child canvas entities render their trixels onto the parent canvas with position offset and depth testing. The system:

1. `beginTick`: binds the compute shader, sets camera offset in frame data
2. `relationTick`: binds the parent canvas textures (images 0, 1)
3. `tick`: binds child canvas textures (images 2, 3), uploads frame data, dispatches compute

## Adding a New Render Stage

1. Write a GLSL compute/vertex/fragment shader in `engine/render/src/shaders/`
2. Add a `SystemName` enum entry in `ir_system_types.hpp`
3. Create a system header in `engine/prefabs/irreden/render/systems/`
4. In the system's `create()`: create named GPU resources, set up the compute dispatch or draw call
5. Register in the creation's RENDER pipeline in the correct order relative to existing stages
