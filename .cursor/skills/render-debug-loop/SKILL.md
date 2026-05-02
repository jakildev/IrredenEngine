---
name: render-debug-loop
description: >-
  Build, run, and visually evaluate a rendering demo in an automated loop.
  Captures screenshots across a demo's configured shot list (zoom, camera
  offset, render-mode combinations), reads each image, diagnoses rendering
  issues against a topic-indexed reference (trixel / SDF shapes, lighting,
  backend parity), applies fixes, and repeats. Use whenever iterating on
  render pipeline bugs, shape alignment, lighting, parity drift, or any
  visual regression in the engine.
---

# Render Debug Loop

Automated build → run → screenshot → evaluate cycle for any Irreden Engine
creation that opts into the `--auto-screenshot` flag.

## Prerequisites

### Platform

Works on all three presets. Use the fleet wrappers so the loop runs
unattended (no command-substitution or compound-command gates).

| Host          | Preset          | Build command                     | Run command              |
|---------------|-----------------|-----------------------------------|--------------------------|
| WSL2 Ubuntu   | `linux-debug`   | `fleet-build --target <TARGET>`   | `fleet-run <EXE_NAME>`   |
| macOS         | `macos-debug`   | `fleet-build --target <TARGET>`   | `fleet-run <EXE_NAME>`   |
| Windows-native| `windows-debug` | see `CLAUDE.md` PATH-fix section  | see `CLAUDE.md`          |

`fleet-build` auto-detects the worktree root and the corresponding
`<worktree>/build/` tree, and configures the preset on first use.
`fleet-run` finds the executable in the build tree and `cd`'s into its
directory before launching, so the demo picks up its sibling `data/`,
`shaders/`, and `scripts/` paths.

### Demo requirement: `--auto-screenshot`

This skill drives a demo that implements the `--auto-screenshot` flag. A
conforming demo:

1. Parses `--auto-screenshot [warmup-frames]` in `main()`.
2. Defines a shot table (zoom, camera offset, optional render mode) and
   cycles through it one shot per screenshot, with settle frames between
   shots.
3. Calls `IRVideo::requestScreenshot()` to capture each shot.
4. Closes the window after the last shot.

**Reference implementation:** `creations/demos/shape_debug/main.cpp`. Look
at `ShotConfig`, `g_shots[]`, and the `AutoScreenshot` system for the
pattern. If your target demo does not yet support `--auto-screenshot`, you
have three options: (a) add it to that demo following the shape_debug
pattern; (b) use `shape_debug` if it exercises the code path you care
about; (c) wait on the auto-screenshot engine helper (GitHub issue
tracked under the `fleet:task` label — search for "auto-screenshot
helper" if promoting shot config into a reusable system).

## Loop Steps

Run sequentially. **Stop after 5 iterations** or when evaluation passes.

### 1. Build

```
fleet-build --target <TARGET>
```

If the build fails, fix compile errors before continuing.

### 2. Clear old screenshots

Screenshots accumulate across runs (the counter does not reset). Before a
new run, remove the previous batch so the Glob in step 4 picks up only
the current iteration. Locate the demo's `save_files/screenshots/`
directory via either:

- **Glob** `build/**/<EXE_NAME>` (or `build/**/<EXE_NAME>.exe` on
  Windows) to find the executable; screenshots live at
  `<dirname of EXE_PATH>/save_files/screenshots/`.
- Read the `fleet-run: <EXE_PATH>` line printed to stdout by the
  previous run's `fleet-run` invocation.

Then `rm -rf` that directory.

### 3. Run with `--auto-screenshot`

```
fleet-run <EXE_NAME> --auto-screenshot 10
```

The demo renders warmup frames, cycles through its configured shots,
captures one screenshot per shot, and closes the window. `fleet-run`
handles the working-directory requirement automatically.

### 4. Read the screenshots

Use the **Glob** tool against
`<demo-cwd>/save_files/screenshots/screenshot_*.png` (or the subdirectory
your demo configures via `IRVideo::configureScreenshotOutputDir`). Sort
by mtime and read the latest batch with the Read tool.

### 5. Evaluate

Check these **always-on** criteria across every screenshot — a bug may
surface at only one zoom, one camera offset, or one render mode.

| Criterion              | What to look for                                         |
|------------------------|----------------------------------------------------------|
| All entities visible   | Expected shapes present, nothing missing                 |
| Correct silhouettes    | Shape outlines match their type                          |
| Consistent shading     | Face shades match expected lighting model                |
| No gaps or overlaps    | Solid faces, no missing pixels or stray dots             |
| Clean edges            | No sawtooth, bowtie, or zigzag along silhouettes         |
| Parity stable          | Edges consistent across different camera offsets         |
| Zoom stable            | No artifacts that appear only at higher zoom levels      |
| Backend parity         | OpenGL and Metal produce visually matching frames        |

Then open whichever diagnosis section below applies to the surface you're
changing.

### 6. Diagnose and fix

Jump to the diagnosis section for your surface. If the symptom doesn't
match any table, widen to all three — bugs often cross surfaces (e.g. a
lighting pass reading a stale trixel canvas).

After applying fixes, return to **Step 1**.

---

## Diagnosis: Trixel / SDF shapes

The original diagnosis surface — applies to anything in the
`VOXEL_TO_TRIXEL_STAGE_*`, `SHAPES_TO_TRIXEL`, `TRIXEL_TO_TRIXEL`, and
`TRIXEL_TO_FRAMEBUFFER` pipeline stages.

### Symptom lookup

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

## Diagnosis: Lighting (T-011 onward)

The lighting stack is being built out in phases — the
`LIGHTING_TO_TRIXEL` pipeline stage has landed (T-011, PR #185); AO
(T-012), directional shadows (T-013), flood-fill propagation (T-014),
LUT palette (T-015), and fog of war (T-016) are the incoming phases.
See `engine/render/CLAUDE.md` for pipeline position.

Populate this section as phases land. For now, the evaluation pattern:

1. Capture a **baseline** screenshot set with lighting disabled (no
   `C_LightSource` in scene, or set `frameData.lightingEnabled_ = 0` in
   `system_lighting_to_trixel.hpp` — that's the CPU short-circuit that
   skips the per-canvas dispatch).
2. Capture the same shots with lighting enabled.
3. Diff: lighting-on frames should modulate voxel and shape canvas
   pixels; **GUI-canvas pixels must be untouched** (T-011 invariant).

### Symptom lookup (to be expanded)

| Symptom | Likely location |
|---------|----------------|
| Lighting pass modulates GUI text/panels | `LIGHTING_TO_TRIXEL` not respecting GUI-canvas bypass |
| No visible lighting effect | Lighting textures unbound, or `isoPixelToPos3D` returning wrong world coords |
| AO missing at voxel junctions (T-012) | `computeAO` neighbor-sampling indices against the 3D occupancy grid |
| Shadow direction wrong (T-013) | Sun-direction uniform vs. shadow-map sweep axis mismatch |
| Torch doesn't light neighbors (T-014) | BFS frontier not seeding emissive voxels, or occupancy grid missing analytic shapes |
| Cel-shade bands smeared (T-015) | LUT sampler filter mode (GL_LINEAR instead of GL_NEAREST) |
| Fog of war reveals through walls (T-016) | LOS ray casting not consulting columnar span lists |

### Baseline-diff screenshots

For each lighting phase, a "lighting on vs. off" pair is worth keeping in
`docs/render-baselines/` as a reference. When a regression appears, diff
new screenshots against the baseline rather than eyeballing.

---

## Diagnosis: Backend parity (OpenGL ↔ Metal)

When a visual defect appears on only one backend, this is a parity
problem, not a pipeline bug. Hand off to the `backend-parity` skill —
its GLSL↔MSL cheatsheet and per-port checklist are the right tool. The
render-debug-loop captures the evidence (before/after screenshots from
both backends); `backend-parity` drives the port.

Common parity-only symptoms:

| Symptom | Likely surface |
|---------|----------------|
| Defect at zoom 1 only on Metal | Dispatch-grid helper returning floor-vs-ceil differently |
| Atomic writes flicker on Metal | `atomicAdd` → `atomic_fetch_add_explicit` memory-order |
| Texture sampling off-by-half-pixel | MSL `sample` vs. GLSL `texelFetch` addressing conventions |
| Buffer binding index wrong on one side | `kBufferIndex_*` constant not mirrored across backends |

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
| `engine/prefabs/irreden/render/systems/system_lighting_to_trixel.hpp` | Screen-space lighting application (T-011) |
| `engine/render/include/irreden/render/ir_render_types.hpp` | GPU struct definitions, ShapeFlags |
| `engine/math/include/irreden/ir_math.hpp` | `trixelOriginOffsetZ1`, `pos2DIsoToTriangleIndex` |

## Notes

- `--auto-screenshot` accepts an optional warmup frame count (default 10).
- Zoom snaps to powers of 2 (range 1–64). Higher zoom may increase
  `effective_subdivisions`, exposing subdivision-specific bugs.
- All entities under test should target the same canvas. Verify by logging
  `canvasEntity_` from component data.
- Pipeline order: compute shaders write canvas textures (depth via
  `imageAtomicMin`, then color) → lighting pass modulates trixel canvas
  pixels → fragment shader reads canvas and draws a full-screen quad per
  canvas into the framebuffer.
- When changing multiple pipeline stages in one PR, capture a screenshot
  set per stage's branch and diff pairwise — it isolates which stage
  introduced the regression faster than re-reading shader source.
