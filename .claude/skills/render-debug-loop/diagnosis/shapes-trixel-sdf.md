# Diagnosis: Trixel / SDF shapes

The original diagnosis surface — applies to anything in the `VOXEL_TO_TRIXEL_STAGE_*`, `SHAPES_TO_TRIXEL`, `TRIXEL_TO_TRIXEL`, and `TRIXEL_TO_FRAMEBUFFER` pipeline stages.

## Symptom lookup

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

## The 2x3 trixel diamond

Each voxel writes **6 canvas pixels** in a 2-wide by 3-tall diamond encoding three isometric faces:

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

- **Write side** (`ir_iso_common.glsl`): `localIDToFace_2x3()` and `faceOffset_2x3(face, subPixel)` map threads/loops to face offsets.
- **Read side** (`f_trixel_to_framebuffer.glsl`): decides which canvas pixel to sample per framebuffer pixel using diagonal parity.

## Correct rendering

- **Smooth staircase edges** — consistent step pattern, slope never reverses.
- **Three face shades** — top x1.25, left x0.75, right x1.0.
- **Solid faces** — no missing pixels inside any face.

## Parity and the "bowtie" artifact

```glsl
int originModifier =
    (z1.x + z1.y +
     int(canvasOffsetFloored.x) + int(canvasOffsetFloored.y)) & 1;
```

Even parity: boundary slope `\`. Odd parity: slope `/`. Adjacent positions must alternate. If parity is globally wrong, all slopes flip — producing a "bowtie" zigzag along what should be a straight diagonal edge.

| Factor | Source | Effect of error |
|--------|--------|-----------------|
| `z1` | `canvasSize / 2 + (-1,-1)` | Flips ALL edges globally |
| `canvasOffset` | `getCameraPosition2DIso()` | Edges flip on odd camera moves |
| Canvas size | Config game resolution | Odd vs even width changes z1 parity |

**Diagnosis:** (1) Check silhouette edges for zigzag. (2) Toggle camera by 1 pixel — if edges flip, it's a `canvasOffset` rounding mismatch. (3) Check `z1.x + z1.y` parity. (4) Compare voxel-pool vs SDF edges.

**Fix locations:**
- `f_trixel_to_framebuffer.glsl` — `originModifier` calculation
- `ir_iso_common.glsl` — `faceOffset_2x3()` offsets
- `ir_math.hpp` / `ir_iso_common.glsl` — `trixelOriginOffsetZ1()`
- `system_shapes_to_trixel.hpp` / `system_voxel_to_trixel.hpp` — camera offset consistency

## Other visual defects

| Defect | Root cause |
|--------|------------|
| Sparse dots instead of solid faces | `faceOffset_2x3` wrong offsets or writing only one subPixel |
| Missing faces | Face loop incomplete or `isInsideCanvas` rejecting edge pixels |
| Wrong depth ordering | `originDistance` not added to `surfaceD` |
| Banding within a face | Double face-shading or unintended checkerboard flag |
| Wrong shape type rendered | `shapeType` enum mismatch between CPU and GPU |
