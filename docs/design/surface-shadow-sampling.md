# Surface-aware detached shadow sampling

Normal revoxelized canvases carry their actual raster phase through presentation,
world depth, lighting and resampled shadow casting. Source casting already has
the authored anchor, and does not apply this correction twice. Source-position
fog consumers keep their original origin. Each displayed local triangle receives
lighting at its geometric centroid.

Finite voxel faces retain their oriented face ID and coordinate basis in the
low byte of the existing 32-bit sun depth word. World-aligned faces use
`0x80 | faceId`; camera-aligned resampled faces use `(faceId << 4) | 0x08`.
IDs 0–5 follow `faceOutwardNormal6`. Analytic finite geometry retains generic
`0x88`. A reserved -8 splat nibble distinguishes these tags from all legacy
splat offsets (-7 through 7); the high 24 depth bits are unchanged. The scalar
shader contract test executes both backends' helpers over every legacy offset,
all face/basis tags, generic finite geometry and the empty sentinel.

An exact detached receiver queries the nearest sun-map cell. For voxel casters,
the stored normal is transformed into world space using its basis flag. Let
`g(n) = (dot(n,u), dot(n,v)) / dot(n,sunDirection)` and let `delta` be receiver
UV minus tap-center UV. The caster depth at the receiver is
`storedDepth + dot(g(casterNormal), delta)`. The receiver depth at the tap is
`receiverDepth - dot(g(receiverNormal), delta)`. Both comparisons must place the
caster in front of the receiver, beyond quantization tolerance and within the
maximum shadow throw. Generic finite geometry uses the receiver normal for
both comparisons. Finite tags never enter this receiver's filtered legacy taps;
those taps still contribute independently.

This agreement test rejects false shadows caused by extrapolating only the
receiver plane onto a neighboring riser. It is conservative, not an exact finite
footprint intersection: an infinite caster plane does not establish coverage,
and disagreement can reject real shadow hits near a face boundary. No new bias,
blur, map allocation or texture lookup is introduced. Remaining false and missed
shadows are retained in the [eight-angle evidence](../pr-screenshots/codex/sun-receiver-footprint/README.md).

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

- [Finite defaults](finite-shadow-defaults.md) cover shape-only and non-main
  producer lifecycles; particle pipelines retain legacy casting.
- Preserve both the four GRID cubes and detached/attached entities in sweeps.
- Main/per-axis exact surface reconstruction and finite edge coverage crossing a
  display triangle remain separate work; do not fix them with a larger blur.
- OpenGL execution still requires a suitable host; shader changes are mirrored.
- Profile GPU stages and CPU update costs with repeated runs before optimizing.
