# Surface-aware detached shadow sampling

Normal revoxelized canvases carry their actual raster phase through presentation,
world depth, lighting and resampled shadow casting. Source casting already has
the authored anchor, and does not apply this correction twice. Source-position
fog consumers keep their original origin. Each displayed local triangle receives
lighting at its geometric centroid.

Finite voxel faces and analytic boxes mark depth samples with low byte `0x88`.
That encoding reserves the otherwise unused splat offset (-8,-8); the CPU splat
radius has a compile-time guard below 8. Legacy direct depth samples remain zero.
An exact detached receiver compares against the finite surface at its nearest
sun-map cell, extrapolating its plane to that sample center. Only depth
quantization tolerance remains in that comparison. Finite tags never enter this
receiver’s filtered legacy taps; those taps still contribute independently.

Main/GRID and raw-debug receivers retain their existing outward offset and PCF
comparison. Their reconstructed raster positions are not yet exact surface
centroids. Applying the new comparison to those positions made an analytic roof
self-shadow and reduced the authored-box yaw-zero oracle from .739 to .684;
that experiment was rejected. This change does not introduce a blur or enlarge
caster geometry. It also does not make shadow-map quantization disappear.

## Native Metal evidence

Full frames are retained under `docs/pr-screenshots/codex/surface-shadow-sampling/`.
Capture recipes are the unchanged commands in
[receiver diagnostics](detached-shadow-receiver-samples.md) and
[the stress audit](canvas-stress-shadow-gaps.md).

| Captures | Check |
|---|---|
| 768–772 | Unit triangle centroid oracle: 30/30 exact RGB matches at five yaws |
| 780–783 | Authored source box: whole-shadow oracle 4/4; IoU .739/.916/.925/.881 |
| 784–786 | Analytic thin roof and detached staircase at 180/202.5/225 degrees; blocked/outside errors both 0 at 180 |
| 787–790 | Unblocked staircase: four cardinal views RGB-identical to shadows-disabled 666–669 |

The original staircase acceptance tolerance is two RGB levels; it is unchanged.
The earlier rejected variants are not shipping: global zero normal offset,
unfiltered finite sampling for raster-origin receivers, and a neighbor-presence
shortcut that suppressed mixed legacy blockers.

## Remaining work

- Enable finite face casting by default with shape-only and non-main producer
  lifecycle coverage, then measure its cost at large populations.
- Preserve both the four GRID cubes and detached/attached entities in sweeps.
- Main/per-axis exact surface reconstruction and finite edge coverage crossing a
  display triangle remain separate work; do not fix them with a larger blur.
- OpenGL execution still requires a suitable host; shader changes are mirrored.
- Profile GPU stages and CPU update costs with repeated runs before optimizing.
