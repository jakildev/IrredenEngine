# Diagnosis: Trixel / SDF shapes

Applies to the `VOXEL_TO_TRIXEL_STAGE_*`, `SHAPES_TO_TRIXEL`, `TRIXEL_TO_TRIXEL`,
and `TRIXEL_TO_FRAMEBUFFER` stages.

## Symptom lookup

| Symptom | Likely location |
|---|---|
| SDF shapes too small / too large | `c_shapes_to_trixel.glsl` effectiveSize or `findSurfaceDepth` |
| Missing faces or sparse dots | `faceOffset_2x3` or canvas pixel write logic |
| Checkerboard mismatch | `isoToLocal3D` vs CPU position-based formula |
| Shapes not rendering | `system_shapes_to_trixel.hpp` gather/dispatch or VISIBLE flag |
| Tile clipping | `dispatchAllShapesTiled` isoMin/isoMax bounds |
| Depth sorting wrong | `encodeDepthWithFace` or `imageAtomicMin` logic |
| Bowtie / zigzag edges | Parity mismatch (below) |
| Edges OK at zoom 1, broken at zoom 4+ | Subdivision or zoom-dependent rounding |
| Edges OK at cam (0,0), broken at (1,0) | Camera offset parity — `canvasOffset` flooring mismatch |
| Wrong position | `C_PositionGlobal3D` not propagated before RENDER |
| Curved shape looks boxy | Wrong `shapeType` enum reaching the GPU |
| Every-other-trixel checkerboard on X/Y faces at non-zero cardinal yaw | Trixel parity ignores the rotated iso frame: `trixelOriginModifier` / `originModifier` vs `rasterYaw`; `localIDToFace_2x3` face/sub-pixel mapping not swapping with cardinal rotation; `f_trixel_to_framebuffer.glsl` parity sampling. Z face usually clean |
| Faces identical at yaw=0 and yaw=π/8 (no inter-cardinal deformation) | Residual yaw deformation matrix collapsing to identity: `emitDeformedFace` `maxN` cap, `IRMath::faceDeformationMatrix` column lengths, world vs detached canvas branch in `c_voxel_to_trixel_stage_1.{glsl,metal}` |
| Half the scene clipped at yaw=0 after a render change | Distance-texture clear regressed: the per-frame `clearTexture` on the distance buffer in `system_voxel_to_trixel.hpp` must run unconditionally — viewport-conditional clears mis-cull at the cull-bounds boundary |
| Geometry pops in/out as camera yaw changes | Chunk visibility mask not rotation-aware: `system_voxel_chunk_visibility.hpp` AABB sweep must use world-space chunk bounds, not iso-space derived at yaw=0 |
| Face colours swapped at exactly 90° / 180° / 270° | Cardinal face-index remapping: `rotateCardinalZ` permutation and `encodeDepthWithFace` face priority under non-zero `rasterYaw` |
| Front/back "occlusion scramble" at non-cardinal yaw under `--depth-color` | Usually the diagnostic's own artifact: the `--depth-color` palette quantises hue in 4/3-unit bands that beat against the 1-unit voxel lattice as moiré, while an SDF twin looks smooth. Re-shoot with `--checkerboard` (the occlusion diagnostic for rotated voxel content); trust `--depth-color` only at cardinal poses or against a voxel reference |

## The 2x3 trixel diamond

Each voxel writes 6 canvas pixels, a 2-wide by 3-tall diamond encoding three
isometric faces:

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

Write side (`ir_iso_common.glsl`): `localIDToFace_2x3()` and
`faceOffset_2x3(face, subPixel)`. Read side (`f_trixel_to_framebuffer.glsl`):
picks the canvas pixel per framebuffer pixel by diagonal parity.

Correct rendering: smooth staircase edges whose slope never reverses; three
face shades (top ×1.25, left ×0.75, right ×1.0); solid faces.

## Parity and the "bowtie" artifact

```glsl
int originModifier =
    (z1.x + z1.y +
     int(canvasOffsetFloored.x) + int(canvasOffsetFloored.y)) & 1;
```

Even parity: boundary slope `\`; odd: `/`; adjacent positions alternate. A
globally wrong parity flips every slope into a bowtie zigzag along straight
diagonals.

| Factor | Source | Effect of error |
|---|---|---|
| `z1` | `canvasSize / 2 + (-1,-1)` | Flips all edges globally |
| `canvasOffset` | `getCameraPosition2DIso()` | Edges flip on odd camera moves |
| Canvas size | Config game resolution | Odd vs even width changes z1 parity |

Diagnose: check silhouettes for zigzag; move the camera by 1 pixel (edges
flipping = `canvasOffset` rounding mismatch); check `z1.x + z1.y` parity;
compare voxel-pool vs SDF edges. Fix locations: `originModifier` in
`f_trixel_to_framebuffer.glsl`; `faceOffset_2x3()` in `ir_iso_common.glsl`;
`trixelOriginOffsetZ1()` in `ir_math.hpp` / `ir_iso_common.glsl`; camera-offset
consistency in `system_shapes_to_trixel.hpp` / `system_voxel_to_trixel.hpp`.

## Other defects

| Defect | Root cause |
|---|---|
| Sparse dots instead of solid faces | `faceOffset_2x3` wrong offsets or only one subPixel written |
| Missing faces | Face loop incomplete or `isInsideCanvas` rejecting edge pixels |
| Wrong depth ordering | `originDistance` not added to `surfaceD` |
| Banding within a face | Double face-shading or unintended checkerboard flag |
| Wrong shape type rendered | `shapeType` enum mismatch between CPU and GPU |
