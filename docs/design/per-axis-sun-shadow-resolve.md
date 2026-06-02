# Per-axis sun-shadow resolve (smooth camera Z-yaw)

**Issue:** #1435 (follow-up to #1370 / #1380). **Status:** implemented.

Restores faithful per-axis voxel **sun-shadow casting** under continuous camera
Z-yaw, which #1380's interim (option C) deferred by stopping the per-axis
canvases from casting at all.

## The invariant

> Per-axis cross-canvas lighting resolves world-pos from **one screen-space
> iso-depth representation**, and cast (bake) and receive read the **same
> source**.

Concretely: the three face-local per-axis voxel canvases are collapsed, once
per frame, into a single screen-space front-most iso-depth texture laid out
**exactly like the main canvas distance texture** (cardinal-snapped iso pixel,
value `pos3DtoDistance << 2 | slot`). `BAKE_SUN_SHADOW_MAP` then casts that
texture through its **existing cardinal recovery**
(`trixelCanvasPixelToWorld3D`), identical to how it bakes the main (SDF/text)
canvas.

## Why a screen-space resolve

The face-local per-axis store (#1310) keys each visible face by its two in-plane
world axes — a dense, collision-free lattice. That representation is correct for
the framebuffer composite (forward scatter) but **cannot** bake directly into
the shared sun depth map: a single voxel block stores its three
mutually-perpendicular faces in three separate canvases, so all three cast into
the shared map and **cross-occlude each other** (#1380's false black side faces
at 135°/225°/315°). The main canvas's cardinal path does not have this problem
because its distance texture is **flattened per screen pixel** — only the
front-most surface per pixel survives `imageAtomicMin`, so a block contributes
exactly one caster per pixel.

`RESOLVE_PER_AXIS_SCREEN_DEPTH` reproduces that per-screen-pixel flattening for
the per-axis voxels: it re-projects each face origin into the cardinal
main-canvas layout and `atomicMin`s, so the resolved texture is
indistinguishable from a cardinal main-canvas distance texture containing the
voxel surfaces. The shared sun map then receives one front-most caster per
screen pixel — no cross-face self-occlusion.

## Pipeline

`RESOLVE_PER_AXIS_SCREEN_DEPTH` runs between the geometry stages and
`BAKE_SUN_SHADOW_MAP`. Two compute passes (both backends — no texture atomics,
since Metal has no portable image-atomic syntax):

1. **scatter** (`c_resolve_per_axis_screen_depth`, dispatched once per axis):
   read each face-local cell, recover the face origin
   (`faceOriginFromInPlane`, the same exact integer inverse
   `perAxisCellToWorld3D` uses), rotate it into the cardinal view frame
   (`rotateCardinalZ` + `cardinalLowerCornerShift × subdivisionScale`,
   mirroring `c_voxel_to_trixel_stage_1`'s cardinal store), and
   `imageAtomicMin` the encoded iso-depth into a screen-space scratch SSBO at
   the cardinal iso pixel `pos3DtoPos2DIso(viewPos)`.
2. **blit** (`c_resolve_per_axis_blit`): materialize the scratch into the
   resolve R32I texture `BAKE` reads, then reset each scratch slot to the empty
   sentinel for next frame (no separate clear dispatch).

The scratch SSBO aliases slot 28 (`SunShadowDepthMap`); the whole resolve stage
runs strictly before `BAKE` rebinds slot 28.

## Cast / receive agreement

`BAKE`'s per-axis dispatch recovers world-pos from the resolve texture via the
cardinal `trixelCanvasPixelToWorld3D`. `COMPUTE_SUN_SHADOW`'s per-axis receive
recovers world-pos via the face-local `perAxisCellToWorld3D`. Both derive from
the **identical** `faceOriginFromInPlane` origin, so cast and receive agree on
world-pos by construction — no shadow acne at the cast/receive boundary.
`COMPUTE_SUN_SHADOW` is therefore unchanged.

## Invariants

- **Cardinal byte-identical.** The per-axis canvases are allocated only at
  non-zero residual yaw, so this stage (and `BAKE`'s per-axis branch) never run
  at a cardinal; `residualYaw == 0` output is unchanged pixel-for-pixel.
- **Graceful degradation.** `C_PerAxisTrixelCanvases::allocate` clears
  `resolveDepth_` to the empty sentinel, so a creation that allocates per-axis
  canvases but does **not** register `RESOLVE_PER_AXIS_SCREEN_DEPTH` reads "no
  per-axis caster" from `BAKE` (the #1380 no-cast behavior) rather than garbage.
- **GLSL / Metal parity.** Both backends ship the scatter + blit kernels. New
  Metal kernels **must** be added to `threadgroupSizeForFunctionName`
  (`metal_pipeline.cpp`) — the default is `1×1×1`, which silently
  under-dispatches a tiled compute pass to a few corner threads.
- **One depth metric.** Resolve, bake, and receive all key by the un-yawed
  `pos3DtoDistance` of the cardinal-view-frame position (the same metric the
  cardinal main-canvas store uses).

## Deferred — north-star unification

Migrating AO + light-volume + sun onto a single screen-space resolve is the
issue's stated north star but triples scope/risk. AO and light-volume keep
their working face-local `perAxisCellToWorld3D` reconstruction (they don't cast
into a shared map, so no self-occlusion). Folded into the detached-canvas
lighting planning family (#1375 / #1376).

## Verification vehicle

`IRVoxelYaw` (#1344) — voxel-set-only Z-yaw scene — registers the resolve stage
and shows voxel cubes casting directional sun shadows under continuous yaw.
`shape_debug` also registers it (cardinal byte-identity + build coverage).
