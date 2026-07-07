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
   (`isoPixelToPos3D`, the same exact iso inverse
   `perAxisCellToWorld3D` uses), rotate it into the cardinal view frame
   (`rotateCardinalZ` + `cardinalLowerCornerShift × subdivisionScale`,
   mirroring `c_voxel_to_trixel_stage_1`'s cardinal store), and
   `imageAtomicMin` the encoded iso-depth into a screen-space scratch SSBO
   across the face's full cardinal-layout **footprint** (#1724): the
   `scale²` micro-cells of the `faceMicroPositionFixed6` u,v sweep, each
   covering its slot's two-pixel `faceOffset_2x3` diamond region, with the
   per-micro-cell depth a real cardinal store would hold. Writing only the
   origin pixel left the resolve ~50% sparse (pinhole casters → dithered,
   gap-riddled shadows).
2. **blit** (`c_resolve_per_axis_blit`): materialize the scratch into the
   resolve R32I texture `BAKE` reads, then reset each scratch slot to the empty
   sentinel for next frame (no separate clear dispatch).

The scratch SSBO aliases slot 28 (`SunShadowDepthMap`); the whole resolve stage
runs strictly before `BAKE` rebinds slot 28.

## Cast / receive agreement

`BAKE`'s per-axis dispatch recovers world-pos from the resolve texture via the
cardinal `trixelCanvasPixelToWorld3D`. `COMPUTE_SUN_SHADOW`'s per-axis receive
recovers world-pos via the face-local `perAxisCellToWorld3D`. Both derive from
the **identical** `isoPixelToPos3D` origin, so cast and receive agree on
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

## Bake coverage: the resolve fill IS the mechanism (#2082 close-out)

The resolve stage's footprint fill is the sun-shadow **coverage** mechanism,
not just a routing step. The per-axis scatter emits the full `scale²`
cardinal-layout footprint per face (#1724) and the world-placed resolve
(#1596) is footprint-dense the same way, so every resolve-fed caster path
already writes a dense sun map. There is **no per-pixel bake-side footprint
splat, and none is needed on these paths**: #2204's deterministic A/B
implemented one and proved it byte-identical on the acceptance scene (even at
a forced 12-texel radius) — the resolve-fed input was already saturated. The
scatter fragment path's analytic edge coverage (`scatterAnalyticEdgeCoverage`,
#1933/#2013, Metal) has no compute-domain call site; the bake cannot "call"
it — a per-fragment helper keyed on `fwidth` and footprint params has no
meaning in the bake's per-pixel sun-space projection (#2082 findings F1/F2).

Two bounded exceptions, both tracked elsewhere:

- **Cardinal direct-bake sparsity is host-conditional (#2270).** The cardinal
  main-bake path (one `atomicMin` per canvas pixel, no resolve between)
  saturates the sun map on some hosts but under-covers on macOS/Metal at 2×
  framebuffer scale — the same splat A/B that was inert in #2204 is strongly
  non-inert there (forced radius 12: 82 → 4 metric components). Deterministic
  captures live on PR #2140's branch; diagnosis + fix belong to #2270, not to
  the resolve architecture.
- **Receiver-side ground-contact stipple** (bias / self-step lane) is
  #2092/#2010; no bake-side coverage change can remove it.

### Accepted residual: ~1-cell per-axis cast-silhouette looseness

The resolve deliberately emits the **cardinal (un-deformed)** face footprint
so `BAKE`'s recovery stays the exact inverse (cast == receive, #1380-safe).
The store packs a sub-cell fractional offset (`encodeDepthWithFaceFrac` bits
[9:2]) that the bake quantizes away, so at residual yaw the cast silhouette
can read up to ~1 cell looser than the visible (deformed) silhouette.
Threading the frac through resolve→bake was considered and **rejected**
(#2082 ruling): it is in direct tension with the exact-inverse cardinal-layout
invariant above. This is accepted drift, mirroring the #1883 precedent —
revisit only if a deterministic capture shows it objectionable in a real
scene.

### Shadow-metric acceptance gates require a deterministic pose

Any acceptance criterion gating on `render-shadow-metric.py` (canvas_stress
`shadow_overlay_floor` or similar) **must pin the pose**:
`--no-auto-rotate --no-spin`, plus `--frozen-pose <rad>` when a rotated
entity pose is the subject. Under the default auto-rotate + self-spin the
metric swings 0.17↔0.99 `largest_frac` run-to-run (#2204), so fixed
thresholds are meaningless without the pinned pose. With it, captures are
byte-identical across runs.
