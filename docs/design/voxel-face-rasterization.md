# Voxel face rasterization: visible-face triplet × exposed-face mask

The voxel-pool → trixel rasterizer emits voxel cube faces into the canvas
distance/color/entity textures. This doc is the source of truth for **which
faces a voxel emits and why**. It supersedes the historical "always emit the
three lower-coordinate faces" model, which was correct only at cardinal yaw 0
and produced the stripe/checkerboard artifact (#1256) at every other cardinal.

Read this before touching `c_voxel_to_trixel_stage_{1,2}`,
`c_voxel_visibility_compact`, `c_compute_voxel_ao`, `c_lighting_to_trixel`,
the `C_VoxelPool` face metadata, or any per-entity rotation work (#1272).

## The six faces are distinct, not three axes with a sign

A voxel cube has six faces. The correct mental model treats them as six
distinct enum values, not three axes each with a ± side:

```
FaceId : X_NEG=0, X_POS=1, Y_NEG=2, Y_POS=3, Z_NEG=4, Z_POS=5
```

The historical raster conflated "the three faces a voxel emits" with "the
three lower-coordinate faces (−X, −Y, −Z)". That conflation is the bug: it
hardcodes the cardinal-0 visible set into the geometry the rasterizer
produces. At cardinal 1 the camera sees +X / −Y / −Z; the +X face is never
emitted by any voxel, the back-of-cube −X face wins the depth tie at the
wrong pixel, Lambert against its inward normal clamps to zero, and the result
is the dark stripe pattern in #1256.

## The model: visible triplet × exposed mask

Exactly three of the six faces are camera-facing at any orientation (the
three whose outward normal has a positive dot with the view direction). Which
three is a pure function of the camera quaternion (cardinal + residual +
pitch). Independently, each voxel has a fixed set of **exposed** faces — those
whose neighbor cell is empty or absent. A face is worth emitting **iff it is
both camera-visible and exposed.**

```
emit(voxel, face)  ⟺  (face ∈ visibleTriplet(cameraQuat))
                       ∧ (face ∈ voxel.exposedFaces)
```

This is correct by construction:

- **Right faces emitted.** The visible triplet is recomputed per frame from
  the camera quaternion, so cardinal yaw, residual yaw, and pitch (PR #1265)
  all shift the triplet automatically. No hardcoded face set anywhere.
- **Only the exterior copy emitted.** Interior voxels have all six faces
  occluded by neighbors → `exposedFaces == 0` → they emit nothing. Along any
  screen-colliding column, only the boundary voxel's face is exposed, so
  there are no interior-boundary copies competing for `atomicMin`. The
  depth-ordering ambiguity that the visible-triplet change alone does NOT
  resolve (interior boundary vs exterior at non-zero cardinal) disappears
  because the interior copies are never emitted.
- **AO / lighting agree by construction.** AO and lighting read the same
  per-face render context (outward normal, in-plane tangents) the rasterizer
  used. There is no second face-label interpretation to drift out of sync —
  the double-rotation bug that #1275's partial fix had to patch in AO and
  lighting cannot recur, because there is one source of face metadata.

## Data flow

### Pool side — exposed-face mask (build/mutate time)

Each voxel in `C_VoxelPool` carries `exposedFaces : uint8` (6 bits used).
On insert: probe the six neighbor cells; set bit `f` for each face whose
neighbor is empty or absent. On insert/remove, update the affected neighbors'
masks too (a new voxel hides its neighbor's facing face; a removed voxel
re-exposes it). Cost is six O(1) spatial-index probes per mutation — the same
neighbor query the visibility-compact pass already performs, so the mask can
be produced in the existing pass rather than a new one.

The mask is camera-independent: it changes only when voxels are added or
removed, never per frame.

### CPU prep — visible-face triplet (per frame)

Resolve `visibleFaces[3]` from the camera quaternion. Each entry is a
`FaceRenderContext` packing:

- `face` — the `FaceId`
- `deformation` — the 2×2 matrix composing cardinal rotation + residual yaw
  (and, for detached / per-entity SO(3), the object rotation). This is the
  generalization of today's `faceDeform[3]` UBO.
- `outwardNormal` — world-frame normal for Lambert
- `tangents` — in-plane tangents for AO neighbor sampling, pre-rotated so the
  neighbor-sample direction lands on the canvas pixel that actually holds the
  +tangent neighbor at this orientation
- format / blend metadata as needed

Uploaded as one small UBO per frame (3 × ~80 bytes).

### Raster — per voxel

The per-voxel emit walks `visibleFaces[0..2]` and branches on the exposed bit:

```glsl
for (int i = 0; i < 3; ++i) {
    FaceId f = visibleFaces[i].face;
    if ((voxel.exposedFaces & (1u << f)) != 0u) {
        emitFace(voxel, visibleFaces[i]);   // deformation, normal, tangents from the context
    }
}
```

Interior voxels skip all three branches and do zero `atomicMin` work — a
throughput win at high voxel counts (a solid 32³ cube does emit work for
~6·32² boundary voxels instead of all 32³).

## Per-entity SO(3) on the main canvas — RETIRED (#1443)

> **Status (#1443):** the per-entity visible-triplet + GPU position-transform
> model described below was implemented as `RotationMode::MAIN_CANVAS_SO3`
> (#1272 / #1299, PR-A) and then **retired**. Attached main-canvas SO(3) is now
> the **GRID re-voxelize** model (`RotationMode::GRID` →
> `SYSTEM_REBUILD_GRID_VOXELS` → `GridRotation::worldCellForGridVoxel`): the
> entity's rotation only changes *which* world cells are filled, and the
> **camera alone** drives trixel deformation + visible-face selection (this
> doc's model, unchanged — the re-voxelized cells are ordinary main-canvas
> voxels). Per-entity trixel deformation does **not** belong on the main
> canvas: a tilted-axis `faceDeformationMatrixSO3` cannot be represented under
> the fixed-(1,1,1) iso-depth-axis invariant (the cube's color drops). Smooth
> per-entity SO(3) trixel deformation lives on **DETACHED** canvases
> (camera-zeroed, exempt from the invariant). If per-voxel-identity-preserving
> *attached* rotation is ever needed, the path is a GPU re-voxelize (fill cells
> on the GPU under the full rotation), not the retired transform + per-entity
> face-deform approach. See #1443 for the decision and the removal.

The retired model computed the visible triplet in the **entity-local** frame
(`visibleTriplet(cameraQuat × entityRotation⁻¹)`), octahedral-snapped the
orientation to one of the 24 cube orientations, and stamped the resulting
triplet per-voxel into `C_Voxel::reserved_` so the raster stages read the
entity's own faces instead of the shared per-canvas `visibleFaceIds_`. The
six-distinct-faces model below is unchanged — it is still exactly what the
**camera** uses to pick faces every frame; only the per-entity indexing was
removed.

## Relationship to the iso-depth-axis invariant

This model does **not** relax the iso-depth-axis invariant
([`iso-depth-axis-invariant.md`](iso-depth-axis-invariant.md)). The GRID
camera is still Z-yaw + cardinal + (PR #1265) pitch-for-detached only; iso
depth is still `x + y + z` in the cardinal-rotated frame. What this model
fixes is the rasterizer's face selection within that invariant — emitting the
faces the camera actually sees at the current cardinal, rather than a
hardcoded set. The depth math is unchanged; only the emitted-face set and the
exposed-face gating are new.

## Smooth camera yaw (between cardinals)

This doc covers *which* faces emit and at which cardinal. Making the camera
rotate **smoothly between** cardinals — interpolating voxel-center positions,
not just deforming face shapes — is a separate architecture: render each
visible face axis to its own deformed trixel canvas and composite the three by
depth at the framebuffer. See
[`per-axis-trixel-canvas-rotation.md`](per-axis-trixel-canvas-rotation.md).

## Migration / status

- **#1256** — the stripe/checkerboard artifact this model fixes.
- **#1275** — the partial fix that escalated `fleet:design-blocked` and led to
  this design. Its `cardinalLowerCornerShift` helper (correct coordinate
  offset after `rotateCardinalZ`) and its AO+lighting face-label fix are
  independently correct preparatory work and should be kept. The design redirect
  changes what comes next: replace the four options in that PR's NEEDS-DESIGN
  comment with this model.
- **#1272 / #1299** — per-entity SO(3) on the main canvas (per-entity visible
  triplet). **Retired in #1443** — attached SO(3) is GRID re-voxelize; see the
  "Per-entity SO(3) on the main canvas — RETIRED" section above.
- **#1265** (merged) — camera pitch via quaternion; the visible-triplet resolver
  reads that quaternion, so pitch shifts the triplet for free on detached
  canvases.
- **#1553 / #1560** (shipped) — **detached** SO(3) is now **GPU re-voxelize**
  too, the detached analogue of the attached GRID re-voxelize above: the
  entity's rotation is rounded into its private pool's voxel cells
  (`c_revoxelize_detached`), which rasterize through the normal single-canvas
  cardinal path. #1560 retired the interim **detached per-axis forward-scatter**
  (`v_peraxis_scatter_detached`, P3a/P3b of epic #1444); the camera/main-canvas
  per-axis path in
  [`per-axis-trixel-canvas-rotation.md`](per-axis-trixel-canvas-rotation.md) is
  untouched.

## What to verify when implementing

1. **Cardinal sweep.** All four cardinals render every cube face the camera
   sees, no stripes, no missing exterior sides. Use the #1271 continuous-yaw
   demo to catch rebracket glitches.
2. **Exposed-mask correctness under mutation.** Add/remove a voxel adjacent to
   another; confirm both masks update so no interior face emits and no exterior
   face is dropped.
3. **AO/lighting parity.** AO and lighting read the same `FaceRenderContext`;
   confirm no double-rotation (the #1275 partial-fix symptom) at non-zero
   cardinal.
4. **Throughput.** Interior voxels emit nothing; measure that a solid cube's
   emit count scales with surface area, not volume.
5. **Attached + detached rotation (re-voxelize).** A rotated entity renders via
   re-voxelize — attached on the main canvas (`SYSTEM_REBUILD_GRID_VOXELS`),
   detached in a private pool (`c_revoxelize_detached`, #1553/#1560). In both,
   the camera drives the faces and the entity's rotation only changes which
   cells fill. The MAIN_CANVAS_SO3 per-entity trixel-deformation path was retired
   in #1443, and the DETACHED per-axis forward-scatter in #1560.
