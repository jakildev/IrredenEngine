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
| Wrong position | `C_WorldTransform` propagation or voxel position upload ordering |
| Curved shape looks boxy | Wrong `shapeType` enum reaching the GPU |
| Every-other-trixel checkerboard on X/Y faces at non-zero cardinal yaw | Trixel parity ignores the rotated iso frame: `trixelOriginModifier` / `originModifier` vs `rasterYaw`; `localIDToFace_2x3` face/sub-pixel mapping not swapping with cardinal rotation; active display consumer and its coordinate mapping. Z face may remain clean |
| Faces identical at yaw=0 and yaw=π/8 (no inter-cardinal deformation) | Residual yaw deformation matrix collapsing to identity: `emitDeformedFace` `maxN` cap, `IRMath::faceDeformationMatrix` column lengths, world vs detached canvas branch in `c_voxel_to_trixel_stage_1.{glsl,metal}` |
| Half the scene clipped at yaw=0 after a render change | Distance-texture clear regressed: the per-frame `clearTexture` on the distance buffer in `system_voxel_to_trixel.hpp` must run unconditionally — viewport-conditional clears mis-cull at the cull-bounds boundary |
| Geometry pops in/out as camera yaw changes | Chunk visibility mask not rotation-aware: `system_voxel_chunk_visibility.hpp` AABB sweep must use world-space chunk bounds, not iso-space derived at yaw=0 |
| Face colours swapped at exactly 90° / 180° / 270° | Cardinal face-index remapping: `rotateCardinalZ` permutation and `encodeDepthWithFace` face priority under non-zero `rasterYaw` |
| Front/back "occlusion scramble" at non-cardinal yaw under `--depth-color` | Usually the diagnostic's own artifact: the `--depth-color` palette quantises hue in 4/3-unit bands that beat against the 1-unit voxel lattice as moiré, while an SDF twin looks smooth. Re-shoot with `--checkerboard` (the occlusion diagnostic for rotated voxel content); trust `--depth-color` only at cardinal poses or against a voxel reference |

## The 2x3 trixel diamond

The unsubdivided cardinal layout encodes three isometric faces in a 2×3
block. Exposure culling and deformation affect which cells are actually written:

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
uses the producer layout: general rectangular canvases read raw texels, while
private voxel canvases reconstruct local triangles. The attached per-axis scatter
reconstructs face quads. Establish the active path before changing either mapping.

For a known planar primitive, inspect silhouette continuity and face boundaries.
Expected shading depends on the configured lights, face normals and render mode.

## Parity and edge artifacts

The triangle-selection helper combines canvas-origin parity, floored canvas
translation and the fractional sample position. Its visual effect depends on
whether the active consumer uses it for display, picking or both. A bowtie or
sawtooth does not establish a parity bug by itself: dilation, cell reconstruction,
face selection and depth can produce similar edges.

Trace the actual write/read coordinate pair before changing `originModifier`.
In particular, detached voxel writes use local canvas coordinates; world placement
of the final quad is a separate transform. Use an isolated primitive and odd-offset
controls to distinguish a parity change from incorrect surface coverage. See
[the detached display investigation](../../../../docs/design/detached-trixel-display.md)
for the raw-gather and dilation experiments, and the
[capture criteria](../references/capture-and-evaluation.md) for judging references.

A consistent centroid/triangle lattice can still display spiky source faces.
For those symptoms, use the independent source-polygon and per-face checks in
[face reconstruction validation](../../../../docs/design/trixel-face-reconstruction-validation.md).
A lattice pass or a valid normal palette alone is not geometry acceptance.

## Other defects

| Defect | Root cause |
|---|---|
| Sparse dots instead of solid faces | `faceOffset_2x3` wrong offsets or only one subPixel written |
| Missing faces | Face loop incomplete or `isInsideCanvas` rejecting edge pixels |
| Wrong depth ordering | `originDistance` not added to `surfaceD` |
| Banding within a face | Double face-shading or unintended checkerboard flag |
| Wrong shape type rendered | `shapeType` enum mismatch between CPU and GPU |
