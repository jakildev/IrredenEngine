---
name: render-debug-loop
description: >-
  Build, run, and visually evaluate a rendering demo in an automated loop.
  Captures screenshots at multiple zoom levels and camera offsets, reads each
  image, diagnoses trixel rendering issues, applies fixes, and repeats. Use
  when iterating on render pipeline bugs, SDF shape alignment, trixel parity,
  or any visual regression in the engine.
---

# Render Debug Loop

Automated build-run-screenshot-evaluate cycle for any Irreden Engine creation
that supports the `--auto-screenshot` flag.

## Prerequisites

- Windows build with MinGW (preset `windows-debug`)
- Build directory at `build/` in the repo root
- DLLs live in `build/`; the run step prepends this to PATH

## Loop Steps

Run these steps sequentially. **Stop after 5 iterations** or when the
screenshot evaluation passes.

### 1. Build

```
cmake --build build --target <TARGET>
```

If the build fails, fix compile errors before continuing.

### 2. Clear old screenshots

```powershell
Remove-Item -Recurse -Force "<BUILD_OUTPUT_DIR>/save_files" -ErrorAction SilentlyContinue
```

### 3. Run with auto-screenshot

```powershell
$env:PATH = "<REPO_ROOT>/build;$env:PATH"
& "<BUILD_OUTPUT_DIR>/<EXE_NAME>.exe" --auto-screenshot 10
```

The demo renders warmup frames, then cycles through its shot configurations
(varying zoom and camera offset), capturing one screenshot per shot with
settle frames between changes. Working directory must be the demo's build
output folder.

### 4. Read the screenshots

Screenshots are numbered sequentially in `<BUILD_OUTPUT_DIR>/save_files/screenshots/`.
Use Glob to find them (the counter does not reset between runs):

```
<BUILD_OUTPUT_DIR>/save_files/screenshots/screenshot_*.png
```

Read the latest batch of `.png` files from this directory.

### 5. Evaluate

Check these criteria across **all screenshots**. A bug may appear at only
one zoom level or camera offset.

| Criterion | What to look for |
|-----------|-----------------|
| All entities visible | Expected shapes present, nothing missing |
| Size match | Compared entities have identical pixel footprint at every zoom |
| Correct silhouettes | Shape outlines match their type (cube, sphere, cylinder, etc.) |
| 3-face shading | Top face bright, left face dark, right face mid-tone |
| No gaps or overlaps | Solid faces, no missing pixels or stray dots |
| Clean edges | No sawtooth, bowtie, or zigzag along silhouettes |
| Parity stable | Edges consistent across different camera offsets |
| Zoom stable | No artifacts that appear only at higher zoom levels |

### 6. Diagnose and fix

If any criterion fails, use the symptom table and trixel rendering reference
below to locate the root cause.

#### Symptom lookup

| Symptom | Likely location |
|---------|----------------|
| SDF shapes too small / too large | `c_shapes_to_trixel.glsl` effectiveSize or `findSurfaceDepth` |
| Missing faces or sparse dots | `faceOffset_2x3` or canvas pixel write logic |
| Checkerboard mismatch | `isoToLocal3D` vs CPU position-based formula |
| Shapes not rendering | `system_shapes_to_trixel.hpp` gather/dispatch or VISIBLE flag |
| Tile clipping | `dispatchAllShapesTiled` isoMin/isoMax bounds |
| Depth sorting wrong | `encodeDepthWithFace` or `imageAtomicMin` logic |
| Bowtie / zigzag edges | Parity mismatch (see below) |
| Edges OK at zoom 1, broken at zoom 4+ | Subdivision or zoom-dependent rounding |
| Edges OK at cam (0,0), broken at (1,0) | Camera offset parity — `canvasOffset` flooring mismatch |
| Wrong position | `C_PositionGlobal3D` not propagated before RENDER |
| Curved shape looks boxy | Wrong `shapeType` enum reaching GPU |

After applying fixes, return to **Step 1**.

---

## Trixel Rendering Reference

### The 2x3 trixel diamond

Each voxel writes **6 canvas pixels** in a 2-wide by 3-tall diamond
encoding three isometric faces:

```
  col 0   col 1
  ┌─────┬─────┐
  │  Z  │  Z  │  row 0   (top face)
  ├─────┼─────┤
  │  Y  │  X  │  row 1   (left / right faces)
  ├─────┼─────┤
  │  Y  │  X  │  row 2
  └─────┴─────┘
```

- **Write side** (`ir_iso_common.glsl`): `localIDToFace_2x3()` and
  `faceOffset_2x3(face, subPixel)` map threads/loops to face offsets.
- **Read side** (`f_trixel_to_framebuffer.glsl`): decides which canvas
  pixel to sample per framebuffer pixel using diagonal parity.

### Correct rendering

- **Smooth staircase edges** — consistent step pattern, slope never reverses.
- **Three face shades** — top x1.25, left x0.75, right x1.0.
- **Solid faces** — no missing pixels inside any face.

### Parity and the "bowtie" artifact

```glsl
int originModifier =
    (z1.x + z1.y +
     int(canvasOffsetFloored.x) + int(canvasOffsetFloored.y)) & 1;
```

Even parity: boundary slope `\`. Odd parity: slope `/`. Adjacent positions
must alternate. If parity is globally wrong, all slopes flip — producing
a "bowtie" zigzag along what should be a straight diagonal edge.

| Factor | Source | Effect of error |
|--------|--------|-----------------|
| `z1` | `canvasSize / 2 + (-1,-1)` | Flips ALL edges globally |
| `canvasOffset` | `getCameraPosition2DIso()` | Edges flip on odd camera moves |
| Canvas size | Config game resolution | Odd vs even width changes z1 parity |

**Diagnosis:** (1) Check silhouette edges for zigzag. (2) Toggle camera by
1 pixel — if edges flip, it's a `canvasOffset` rounding mismatch.
(3) Check `z1.x + z1.y` parity. (4) Compare voxel-pool vs SDF edges.

**Fix locations:**
- `f_trixel_to_framebuffer.glsl` — `originModifier` calculation
- `ir_iso_common.glsl` — `faceOffset_2x3()` offsets
- `ir_math.hpp` / `ir_iso_common.glsl` — `trixelOriginOffsetZ1()`
- `system_shapes_to_trixel.hpp` / `system_voxel_to_trixel.hpp` — camera offset consistency

### Other visual defects

| Defect | Root cause |
|--------|------------|
| Sparse dots instead of solid faces | `faceOffset_2x3` wrong offsets or writing only one subPixel |
| Missing faces | Face loop incomplete or `isInsideCanvas` rejecting edge pixels |
| Wrong depth ordering | `originDistance` not added to `surfaceD` |
| Banding within a face | Double face-shading or unintended checkerboard flag |
| Wrong shape type rendered | `shapeType` enum mismatch between CPU and GPU |

---

## Key Files

| File | Role |
|------|------|
| `engine/render/src/shaders/c_shapes_to_trixel.glsl` | SDF shape compute shader |
| `engine/render/src/shaders/ir_iso_common.glsl` | Shared iso math (face offsets, depth encoding) |
| `engine/render/src/shaders/f_trixel_to_framebuffer.glsl` | Fragment shader: canvas to framebuffer with parity |
| `engine/render/src/shaders/c_voxel_to_trixel_stage_1.glsl` | Voxel pool stage 1 (reference) |
| `engine/render/src/shaders/c_voxel_to_trixel_stage_2.glsl` | Voxel pool stage 2 (reference) |
| `engine/prefabs/irreden/render/systems/system_shapes_to_trixel.hpp` | CPU-side shape gather and dispatch |
| `engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp` | CPU-side framebuffer draw, zoom, camera |
| `engine/render/include/irreden/render/ir_render_types.hpp` | GPU struct definitions, ShapeFlags |
| `engine/math/include/irreden/ir_math.hpp` | `trixelOriginOffsetZ1`, `pos2DIsoToTriangleIndex` |

## Notes

- `--auto-screenshot` accepts an optional warmup frame count (default 10).
- Zoom snaps to powers of 2 (range 1–64). Higher zoom may increase
  `effective_subdivisions`, exposing subdivision-specific bugs.
- All entities under test should target the same canvas. Verify by logging
  `canvasEntity_` from component data.
- Pipeline order: compute shaders write canvas textures (depth via
  `imageAtomicMin`, then color) -> fragment shader reads canvas and draws
  a full-screen quad per canvas into the framebuffer.
