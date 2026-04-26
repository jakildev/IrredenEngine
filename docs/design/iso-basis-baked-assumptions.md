# Iso-basis bake-ins (T-054 audit)

The trixel rasterization pipeline assumes a fixed isometric projection
basis — `iso.x = -x + y; iso.y = -x - y + 2z` — applied uniformly to
every voxel and shape on every frame. This audit enumerates the places
that bake the basis in as a constant so downstream Z-yaw work
(`#310` epic, T-055/T-056/T-057) can either:

- Replace the constant with a `rasterYaw`-indexed basis (cardinal-snap
  raster path), or
- Compose the constant with a continuous `visualYaw` rotation (SDF and
  picking paths).

T-054 plumbs `visualYaw / rasterYaw / residualYaw` through the
camera-side machinery and into the relevant GPU UBOs. **No bake-in
listed here has been removed yet** — the field is uploaded to the GPU
but no shader reads it. Each row is a target for a follow-up task.

## CPU-side bake-ins

| Location | What is baked | Downstream task |
|---|---|---|
| `engine/math/include/irreden/ir_math.hpp:213-220` (`pos3DtoPos2DIso`) | The iso projection matrix is hardcoded as `(-x+y, -x-y+2z)`. Used by every CPU-side iso conversion. | T-057 picking inverse must compose `R(-rasterYaw)` before this. |
| `engine/math/include/irreden/ir_math.hpp:225-228` (`pos3DtoPos2DScreen`) | Calls `pos3DtoPos2DIso` directly; same bake-in inherited. | Same as above. |
| `engine/math/include/irreden/ir_math.hpp:307-329` (`entityIsoBounds`) | Computes 8-corner iso AABB using the fixed basis. Used by render culling. | Cardinal-snap raster (#311 / T-055) keeps the cull viewport consistent with the active raster basis. |
| `engine/math/include/irreden/ir_math.hpp:275-290` (`visibleIsoViewport`) | Inverts the canvas-pixel formula assuming the fixed iso basis. | Cull viewport must be derived from `rasterYaw` once the raster basis rotates. |

## GPU-side bake-ins (GLSL + Metal counterparts)

| Location | What is baked | Downstream task |
|---|---|---|
| `engine/render/src/shaders/ir_iso_common.glsl:9-14` and `metal/ir_iso_common.metal:15` (`pos3DtoPos2DIso`) | Root iso projection in GPU; mirrors the CPU helper. Every consumer below depends on this. | T-055 introduces a 4-permutation variant indexed by `rasterYaw`. T-056 keeps the SDF path on the continuous `visualYaw`. |
| `engine/render/src/shaders/ir_iso_common.glsl:24-29` and `metal/ir_iso_common.metal:29` (`isoPixelToPos3D`) | Inverse projection from iso pixel + depth back to 3D world. Used by AO, sun shadow, lighting, fog reconstruction. | Must compose `R(-rasterYaw)` after the inverse to recover the pre-rotation world position. Sun-shadow / lighting consumers rely on this — see "Sun shadow / lighting" below. |
| `engine/render/src/shaders/ir_iso_common.glsl:5-7` (face constants `kXFace=0, kYFace=1, kZFace=2`) and `localIDToFace_2x3`, `faceMicroPositionFixed`, `faceOffset_2x3` | Face indices encode which world axis a trixel sub-pixel belongs to. The face → world-axis mapping rotates discontinuously at cardinal swaps. | Plan §"Procedural face brightness flip": acceptable for v1; future fix derives face brightness from the post-rotation world face direction. |
| `engine/render/src/shaders/ir_iso_common.glsl:44-49` (`adjustColorForFace`) | Z=1.25, X=1.0, Y=0.75 brightness baked per face index. Same artifact as above. | Same — flagged as v1 artifact. |
| `engine/render/src/shaders/c_voxel_to_trixel_stage_1.glsl:71-76` and `_stage_2.glsl:75-80` | Each voxel writes to canvas pixels via `pos3DtoPos2DIso(voxelPositionInt)` with no rotation. | T-055 picks one of 4 cardinal basis permutations from `rasterYaw`. |
| `engine/render/src/shaders/c_shapes_to_trixel.glsl` (entire SDF dispatch) | SDF marches along the (1,1,1) iso depth axis, fixed. | T-056 rotates the world-space SDF query by `visualYaw` (continuous; no integer constraint). |
| `engine/render/src/shaders/c_voxel_visibility_compact.glsl:54-58` | Cull test in iso space against `cullIsoMin/Max` from CPU. | Cull viewport derivation already moves with `rasterYaw` (see CPU side). Shader only consumes the bounds; no shader change needed. |

## Sun shadow / lighting

The architectural plan flagged sun-shadow / lighting as the first thing
to check for `yaw=0` assumptions. Result of the audit:

- **Sun direction is world-space** —
  `engine/prefabs/irreden/render/systems/system_compute_sun_shadow.hpp:36-39`
  and the corresponding GPU UBO `FrameDataSunShadow.sunDirection` is a
  unit vector in **world coordinates**, independent of the camera basis.
  Shadow marching iterates the world-space occupancy grid
  (`engine/render/src/shaders/c_compute_sun_shadow.glsl`) along that
  world-space axis. **Yaw rotation does not invalidate the shadow
  direction.**
- **But surface-position reconstruction is yaw-dependent** —
  `c_compute_sun_shadow.glsl` and `c_compute_voxel_ao.glsl` both
  reconstruct the surface 3D position from the encoded distance via
  `isoPixelToPos3D`. That helper assumes the fixed iso basis. **Once
  the trixel raster runs at `rasterYaw ≠ 0`, every reconstructed
  position must be rotated by `R(-rasterYaw)` before sampling the
  occupancy / AO grids.**
- **Lighting volume composite is similarly affected** —
  `c_lighting_to_trixel.glsl` reconstructs the world position to sample
  the 3D light volume. Same fix.

**Recommendation for downstream tasks:** introduce an
`isoPixelToWorld3D(int isoX, int isoY, float depth, float rasterYaw)`
helper in `ir_iso_common.glsl/.metal` that internally calls the
existing `isoPixelToPos3D` and then applies `R(-rasterYaw)`. Replace
all `isoPixelToPos3D` usages in lighting passes with the new helper
once T-055 lands. Shadow direction itself stays untouched.

This is **not a blocker for T-054** — at `visualYaw = 0`,
`rasterYaw = 0` and `R(-rasterYaw) = I`, so the existing math is
already correct. The follow-up only matters once T-055 starts moving
`rasterYaw` away from zero.

## Out of scope for this audit

- Pitch and roll. T-054 covers Z-yaw only; the other two axes would
  rotate the iso depth axis itself and break the `(1,1,1)` depth
  invariant the rasterizer depends on.
- Continuous (non-cardinal) voxel rasterization. The plan's "Option C"
  — fractional trixel raster — would lose the O(1) optimization and is
  filed as a v2 epic.
- Per-canvas yaw. All canvases share the engine's single camera entity
  and hence a single `visualYaw`. Per-canvas yaw would require a
  `C_CameraYaw` per canvas-camera and per-canvas frame-data uploads;
  not currently planned.
