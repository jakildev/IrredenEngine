# Iso-depth-axis invariant: why world-camera rotation is Z-yaw-only for GRID entities

The (1,1,1) world-space axis is the iso-projection's depth axis. Every
GRID-mode consumer in the renderer — picking, hitbox, gizmo drag, SDF
analytic AABB, the integer trixel raster — bakes that geometry into a
shortcut. Rotating the world camera around any axis other than world-Z
breaks at least one of those shortcuts silently. DETACHED entities are
exempt because they raster through `faceDeformationMatrixSO3`, which is
axis-agnostic, and the per-canvas SO(3) bake (T-291 / T-295) absorbs
arbitrary camera rotation downstream of this contract.

This doc exists so a future free-camera epic (orbit, perspective preview,
cinematics) can cost the work before scoping it. It is not a roadmap.
The companion task to expand the camera's rotation surface for DETACHED
entities — without touching GRID — is filed as issue #1076 (camera SO(3)
quaternion). #1075 / T-319 lands the composition path that makes
camera rotation reach DETACHED canvases.

## What the invariant is

The iso projection in [`engine/math/CLAUDE.md` §"Isometric projection — the
equations"](../../engine/math/CLAUDE.md#isometric-projection--the-equations)
maps world `(x, y, z)` to screen-iso `(-x + y, -x - y + 2z)` and to
depth `x + y + z`. The depth scalar is the dot product of the world
position with the unit (1,1,1) direction (modulo a `√3` scale): all
points sharing a depth value lie on a plane perpendicular to (1,1,1).
That perpendicular is the **iso depth axis**.

Rotating the world camera around world-Z (yaw) permutes the X/Y axes
but leaves Z untouched; the iso depth axis (1,1,1) rotates with the
world, and a cardinal-snapped rotation (multiple of π/2) maps it back
onto itself (modulo sign). Pitch and roll do not. Once the camera
rotates around X or Y, the depth axis no longer points along
(1,1,1) in *world* coordinates — every "depth = x + y + z" shortcut is
geometrically wrong.

The continuous Z-yaw is split into a cardinal-snap component
(`rasterYaw`, a multiple of π/2) and a sub-cardinal residual
(`residualYaw`, in [−π/4, π/4]). The cardinal snap is what every
GRID-side consumer rotates by; the residual was historically composited
in screen space (`SCREEN_SPACE_RESIDUAL_ROTATE`) and has since moved
inside the per-canvas raster via face-deformation (T-293). T-323
finalizes the cleanup of the legacy passthrough. The (1,1,1) iso-depth
axis is invariant under both halves so long as the rotation stays
Z-axis.

## Why GRID entities depend on it

The integer trixel rasterizer (`c_voxel_to_trixel_stage_1.glsl`,
`c_shapes_to_trixel.glsl`) picks one of four cardinal-snapped basis
permutations from `rasterYaw` via
[`IRMath::rasterYawCardinalIndex`](../../engine/math/include/irreden/ir_math.hpp)
(see lines 252–267) and rasterizes at integer iso coordinates. The
inverse of that projection — recovering a world point from an iso pixel
and a depth scalar — only has a closed form when the depth axis is
(1,1,1) in *world* coordinates after the cardinal rotation, because the
inverse `isoPixelToPos3D` solves three linear equations for three
unknowns and one of them is `x + y + z = depth`. A non-Z-axis rotation
makes that third equation un-pin the third coordinate.

The depth walk in CPU picking
([`engine/prefabs/irreden/render/picking.hpp`](../../engine/prefabs/irreden/render/picking.hpp)
lines 65–75 and 211–234) steps along the canvas-frame iso depth axis in
`kPickingDepthStep = 0.5` increments, and the voxel iso-depth bound
formula at lines 169–180 — `center = rotatedCenter.x + rotatedCenter.y +
rotatedCenter.z`; `halfRange = bboxHalf.x + bboxHalf.y + bboxHalf.z` —
projects the AABB half-extents onto (1,1,1) directly. The comment at
line 172 names this explicitly: "Cardinal Z rotation permutes axes; the
sum of half-extents (projected onto (1,1,1)) is rotation-invariant."
Pitch/roll breaks that sum.

The SDF shader's analytic AABB cull in
[`engine/prefabs/irreden/render/systems/system_shapes_to_trixel.hpp:421`](../../engine/prefabs/irreden/render/systems/system_shapes_to_trixel.hpp)
expands the XY AABB by `|cosYaw|·halfX + |sinYaw|·halfY` (and the
symmetric form for Y) — a 2D rotation only. The Z half-extent is
passed through unchanged because the GPU shapes path assumes the
depth axis is the world Z axis. The GPU twin at
[`c_shapes_to_trixel.glsl:195–207`](../../engine/render/src/shaders/c_shapes_to_trixel.glsl)
and `:677–690` makes the same assumption for the box-slab solver and
the sphere/cylinder analytic shortcuts ("Sphere: rotation-invariant
under z-yaw"; "Cylinder: z-axis aligned"). Pitch or roll invalidates
every one of those analytical paths.

Hitbox cardinal-snap
([`system_hitbox_mouse_test.hpp:57`](../../engine/prefabs/irreden/input/systems/system_hitbox_mouse_test.hpp)
and gizmo drag
[`system_gizmo_drag.hpp:289,296`](../../engine/prefabs/irreden/render/systems/system_gizmo_drag.hpp))
consult `rasterYawCardinalIndex` — a 4-state enum
(`k0 / k90 / k180 / k270`). A non-Z-axis rotation has no cardinal
quotient and the enum can't represent it.

## Why DETACHED entities don't

A DETACHED entity stores its rotation in
`C_LocalTransform.rotation_` as a full quaternion. The voxel raster
applies a per-canvas SO(3) bake via `faceDeformationMatrixSO3` (T-291
/ T-295) before projection, which is axis-agnostic — pitch, roll, and
yaw all compose into the canvas bake without losing precision. The
post-T-319 composition (PROPAGATE_CANVAS_ROTATION) folds the world
camera's full rotation into the per-canvas bake, so once the camera
grows pitch/roll for issue #1076, DETACHED entities pick up the
additional rotation through the composition chain. No change is
needed at the consumer site — they already read the full quat.

The depth-walk and AABB-projection shortcuts above do not apply to
DETACHED rasterization because the per-canvas bake collapses the
world-to-screen transform into a 2x2 (face deformation) instead of
relying on the (1,1,1) depth-axis algebra.

## Full call-site map

Re-greppable with `Z-yaw|rasterYaw|cardinalIndex|rotation-invariant` in
`engine/` + `creations/`; the table below is a fresh point-in-time
snapshot. Anything new should be added here in the same PR that
introduces the dependency.

### CPU (C++) consumers

| File:line | Surface | What it bakes |
|---|---|---|
| `engine/math/include/irreden/ir_math.hpp:170–181` (`isoPixelToPos3D`) | iso inverse | Closed-form inverse assumes (1,1,1) depth axis: solves `x+y+z = depth` as the third equation. Any non-Z-axis rotation breaks the inverse. |
| `engine/math/include/irreden/ir_math.hpp:250–267` (`CardinalIndex`, `rasterYawCardinalIndex`) | cardinal-snap enum | 4-state enum; no representation for non-Z rotations. Picking ↔ raster handshake hinges on CPU + GPU resolving the same index. |
| `engine/math/include/irreden/ir_math.hpp:269–276` (`rotateCardinalZ`) | cardinal basis permutation | Operates only on Z-rotated cardinal index; X/Y rotations have no analogue. |
| `engine/prefabs/irreden/render/picking.hpp:65–75` (`kPickingDepthStep`, `kPickingDepthMargin`) | depth-walk granularity | Step is "along the canvas-frame iso depth axis"; the constants only make sense if that axis remains (1,1,1). |
| `engine/prefabs/irreden/render/picking.hpp:169–180` (voxel iso-depth bounds) | per-set AABB pre-filter | Bound formula projects AABB half-extents onto (1,1,1) as a scalar sum. Cited explicitly as "rotation-invariant" under cardinal Z. |
| `engine/prefabs/irreden/render/picking.hpp:211–234` (`castVoxelRay`) | ray walk | "Walk along the canvas-frame iso depth axis in `kPickingDepthStep` increments over the union of all visible shape and voxel-set iso-depth ranges." |
| `engine/prefabs/irreden/render/camera.hpp:18–76` (`IRPrefab::Camera::computeYawSplit`) | Z-yaw split | Splits a scalar `visualYaw` into `(rasterYaw, residualYaw)`; both halves are scalars, not quats. No room for X/Y components. |
| `engine/prefabs/irreden/render/camera.hpp` (`IRPrefab::Camera::getRotationQuat` / `getYaw`) | camera-side surface | Camera rotation lives on `C_LocalTransform.rotation_` (issue #1076 / T-364 retired the `C_CameraYaw` component). GRID consumers read `getYaw()` which extracts the Z-component via `IRMath::quatExtractZAngle`; DETACHED consumers read the full quat through `getRotationQuat()`. |
| `engine/prefabs/irreden/input/systems/system_hitbox_mouse_test.hpp:40–58` (cardinal-index hitbox test) | hitbox rotation | Calls `IRPrefab::Camera::getRasterYaw()`; rotates world position via `rotateCardinalZ` to map to iso space. |
| `engine/prefabs/irreden/render/systems/system_gizmo_drag.hpp:287–299` (`canvasIsoDepthOfAnchor`, `anchorIsoPosition`) | gizmo anchor projection | Same `rasterYawCardinalIndex + rotateCardinalZ + pos3DtoPos2DIso` sequence. |
| `engine/prefabs/irreden/render/systems/system_shapes_to_trixel.hpp:405–432` (SDF cull bounds) | per-shape iso AABB | XY AABB expansion under Z-yaw only; Z half-extent passes through unchanged. |
| `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` + `system_shapes_to_trixel.hpp` (per-frame FrameData upload) | UBO upload | Each per-frame UBO ships `visualYaw / rasterYaw / residualYaw` as three floats derived from `IRPrefab::Camera::getYaw()` + `computeYawSplit`. Adding pitch/roll requires either a quaternion upload or two more split floats. |
| `creations/editors/voxel_editor/main.cpp` (camera rotation consumer) | creation-side | Uses `IRPrefab::Camera::rotateYaw` / `getYaw` through the helper API. Anchors its one-shot per-frame system on `C_Camera` since T-364 retired `C_CameraYaw`. |
| `creations/demos/z_yaw_rotation/main_mouse.cpp`, `main_static.cpp` | creation-side | Same; demo-level camera control. |
| `test/render/camera_yaw_test.cpp` | tests | Will need pitch/roll equivalents under #1076. |

### GPU (shader) consumers

| File:line | Surface | What it bakes |
|---|---|---|
| `engine/render/src/shaders/ir_iso_common.glsl:224–276` + `metal/ir_iso_common.metal` | shader iso primitives | `rasterYawCardinalIndex`, `rotateCardinalZ[Inv][I]`, `isoPixelToWorld3D` — GPU mirrors of the CPU helpers. Same Z-axis-only semantics. CPU + GPU must agree on the cardinal index. |
| `engine/render/src/shaders/c_voxel_to_trixel_stage_1.glsl:20–42` (UBO struct) | per-frame yaw upload | `visualYaw` is "continuous Z-yaw (radians)" — single float, no axis component. Stages 1 and 2 both read these fields; the Metal twin matches. |
| `engine/render/src/shaders/c_shapes_to_trixel.glsl:195–207` (`boxSlabIntersectYaw`) | SDF box-slab solver | "z-axis is rotation-invariant under z-yaw" — the box-slab math derives only XY coefficients from `(yawC, yawS)` and treats Z as unrotated. Metal twin (`metal/c_shapes_to_trixel.metal:836–855`) carries the same assumption. |
| `engine/render/src/shaders/c_shapes_to_trixel.glsl:677–690` (`findSurfaceDepth` dispatcher) | sphere/cylinder shortcuts | "Sphere: rotation-invariant (\|p\| under z-yaw unchanged); analytical works at any yaw without modification. Cylinder: z-axis aligned, \|p.xy\| invariant under z-yaw; same." The shortcuts collapse under non-Z rotation. |
| `engine/render/src/shaders/c_voxel_to_trixel_stage_1.glsl:30` | UBO comment | "continuous Z-yaw (radians)" — single-axis assumption documented in the upload contract. |
| `engine/render/src/shaders/c_compute_sun_shadow.glsl`, `c_compute_voxel_ao.glsl`, `c_lighting_to_trixel.glsl`, `c_fog_to_trixel.glsl` (and Metal twins) | surface-position reconstruction | Call `isoPixelToWorld3D(isoX, isoY, depth, cardinalIndex)` to recover world coords; the inverse only works for cardinal Z rotations. See `iso-basis-baked-assumptions.md` §"Sun shadow / lighting" for the per-shader detail. |

## "How to break it" cost map

| Site | Cost to widen to full SO(3) for GRID entities |
|---|---|
| `IRMath::isoPixelToPos3D` | **Rewrite.** No closed-form inverse for non-(1,1,1) depth axes; would need a per-pixel ray cast (3D from 2D iso + a continuous depth scalar) or a different projection family. |
| `IRMath::CardinalIndex` / `rasterYawCardinalIndex` / `rotateCardinalZ` | **Rewrite.** 4-state enum has no SO(3) analogue; either generalize to a full quaternion upload + GPU-side basis derivation, or accept lossy quantization. |
| `picking.hpp` depth walk | **Rewrite.** `kPickingDepthStep` semantics presume monotonic depth along (1,1,1); under pitch the depth-axis component varies per pixel, requiring a per-ray-direction step computation. |
| `picking.hpp` voxel iso-depth bound (sum of half-extents projected onto (1,1,1)) | **Hard.** Replace with a per-orientation AABB projection: compute the rotated AABB's 8 corners, dot each with the current depth-axis direction, take min/max. Cheap per shape but invalidates the "rotation-invariant" optimization. |
| `system_gizmo_drag.hpp`, `system_hitbox_mouse_test.hpp` cardinal-index calls | **Hard.** Both consume the cardinal-index API. Once that's generalized they need quat-based rotation of the world position before iso projection, which compounds with the iso-inverse rewrite above. |
| `system_shapes_to_trixel.hpp` SDF cull bounds (XY-only AABB expansion) | **Hard.** Generalize the AABB expansion to 3D for a full rotation; the cull becomes a rotated-OBB iso projection, similar to the picking bound rewrite. |
| `c_shapes_to_trixel.glsl` box-slab + sphere/cylinder shortcuts | **Hard.** The shortcuts assume Z-axis invariance. Box-slab generalizes to a full 3x3 rotation of the per-axis coefficients (manageable); sphere/cylinder shortcuts collapse to the general SDF path. Performance cost depends on how many on-screen shapes hit the general path. |
| `c_shapes_to_trixel.glsl` general SDF march | **Easy.** The general SDF path already rotates the query point by `R_z(+rasterYaw)`; replacing with a quat rotation is mechanical. Performance is unchanged. |
| `c_voxel_to_trixel_stage_1.glsl` UBO struct + raster | **Hard.** The shader picks one of four cardinal basis permutations from `cardinalIndex`. Generalizing to SO(3) requires either a per-canvas basis upload or a runtime basis derivation per dispatch; the integer raster invariant has to be re-justified. |
| `c_compute_sun_shadow.glsl`, `c_compute_voxel_ao.glsl`, lighting/fog reconstruction | **Hard.** All rely on `isoPixelToWorld3D` which assumes the cardinal-Z inverse. The inverse rewrite cascades here. |
| `IRPrefab::Camera::*` accessors + camera entity's `C_LocalTransform.rotation_` | **Done (T-364).** Camera rotation now lives on `C_LocalTransform.rotation_`; the accessors extract the Z-component for GRID consumers and expose the full quat for DETACHED. The Z-extraction path keeps GRID behavior bit-identical (`quatAxisAngle(z, y)` / `quatExtractZAngle` round-trip is exact for pure-Z). |
| `creations/editors/voxel_editor` + `z_yaw_rotation` demo + `test/render/camera_yaw_test.cpp` | **Done (T-364).** All read camera rotation through the `IRPrefab::Camera::` helpers; archetype filters for one-shot per-frame ticks anchor on `C_Camera` instead of `C_CameraYaw`. |
| DETACHED entity rasterization (every shader path that uses `faceDeformationMatrixSO3`) | **Free.** Already axis-agnostic. Picks up pitch/roll for free once #1076 composes the camera quat into the per-canvas bake (via #1075's composition step). |

The total cost of breaking the invariant for GRID is dominated by the
iso-inverse rewrite (`isoPixelToPos3D` and `isoPixelToWorld3D` in
shaders) and the cardinal-index generalization. Both are foundation
work that every other rewrite cascades from. Anything labelled
"Rewrite" above blocks the rest.

For any future free-camera epic: scope as **DETACHED-only first** (free,
via #1076). GRID-mode pitch/roll is a multi-PR rewrite touching the
iso-inverse, the cardinal-index API, the picking/hitbox/gizmo cardinal
consumers, and the SDF AABB cull — sized similarly to T-054 / T-055
combined. Don't promise either in the same release.

## Related docs

- [`docs/design/iso-basis-baked-assumptions.md`](iso-basis-baked-assumptions.md) — T-054 audit of what is baked into the iso basis (the consumer-side rasterYaw enabling work).
- [`docs/agents/CLAUDE-BASELINE.md`](../agents/CLAUDE-BASELINE.md) — engine-wide ECS/style conventions.
- Issue #1075 (T-319, PR #1087) — `PROPAGATE_CANVAS_ROTATION` composes camera yaw into per-canvas SO(3); the composition step that lets DETACHED entities pick up future camera pitch/roll.
- Issue #1076 — camera SO(3) quaternion surface for DETACHED rotation; A/B decision still pending an opus-architect call.
