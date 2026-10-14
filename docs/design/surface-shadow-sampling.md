# Surface-aware voxel and detached shadow sampling

## Settled-cardinal voxel receivers

The single-canvas cardinal voxel route reconstructs the center of the emitted
finite face sample with `cardinalVoxelSurfaceCenter`. It removes the stored
half's `faceOffset_2x3` before inverting iso position and decoded depth. Integer
micro-origins have even iso parity; the Y pair's odd starting parity identifies
its half. Both pixels of a face sample therefore recover the same surface point.

In subdivision modes, the stored depth identifies the face plane, including its
polarity. The receiver adds half a microcell along each in-plane axis, divides
by effective density, subtracts `kVoxelRasterCellAnchor` in the view frame, and
rotates back to world space. NONE mode instead recovers the unit voxel center
and adds half its signed face normal. These are face-sample centers, not
continuous positions for every presentation fragment.

Eligibility requires positive producer provenance. High-word bit 18 of the
entity-ID carrier, `kEntityIdCardinalVoxelMaskInHighWord`, marks the winning
non-detached voxel color/ID publication at zero residual yaw. Shared entity-ID
decoding strips it alongside the other carrier fields. Shape, text and other
producer writes do not acquire the marker; a missing retained shape owner is
not evidence that a sample came from a voxel.

`c_compute_sun_shadow_body` selects this receiver only at zero residual yaw,
with the marker present and `sunBasisU.w != 0`. The latter carries the frame's
finite-caster-coverage gate; legacy and particle caster frames retain their
existing receiver path. The selected route calls `worldSurfaceSunShadowFactor`
with the recovered surface and its signed world normal. Cascade selection uses
`pos3DtoDistance(surface)` in world voxel units, matching `perAxisSunShadowFactor`,
rather than the cardinal sample's subdivided stored depth. Per-axis, detached,
shape and unmarked raster receivers retain their own contracts; this does not
remove normal offsets or filtered sampling globally.

The connected `test_render_cardinal_voxel_receiver.py` adapter compares production
publication and reconstruction with independently averaged face vertices for
cardinal rotations, both halves and polarities, NONE/subdivision modes and camera
offsets. Carrier tests cover identity and existing flag preservation. The
[cardinal receiver evidence](../pr-screenshots/codex/4231-cardinal-surface-receiver/README.md)
records the matched 720-frame sweep and rerun box, roof and staircase controls.
The historical captures below validate the earlier detached work only.

## Detached receivers and finite fallback

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

Unmarked raster receivers retain their existing outward offset and PCF
comparison. Their reconstructed raster positions are not exact surface
centroids. Applying the finite comparison directly to those positions made an
analytic roof self-shadow and reduced the authored-box yaw-zero oracle from .739 to .684;
that experiment was rejected. The cardinal route above first recovers the
actual voxel face sample and requires its producer marker. Neither route
introduces a blur or enlarges caster geometry; shadow-map quantization remains.

## Historical detached Metal evidence

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

Finite voxel casters of all three bases (world GRID, camera-aligned resampled,
and rigid source) also retain their actual projected face quads in the bounded
query index. A complete tile uses ray/quad intersections at the receiver point;
its indexed map layer is ignored after an exact miss. Eligible analytical
sphere, cylinder, cone and torus casters also publish typed finite records;
other casters stay in the independent primary layer. Incomplete tiles use the
indexed fallback layer, which preserves world/camera face markers for the plane-agreement calculation
above. Raster-origin receivers retain the minimum of both depth layers.

Only faces overlapping at least one cascade consume records. Capacity remains
65,536 records and 64 candidates per tile, shared by all voxel bases. Added GRID
or resampled geometry can force previously exact rigid-source tiles into
approximate fallback; this is not a guarantee of sharp edges at arbitrary density.
Native sparse-scene acceptance and outstanding limits are recorded in the
[finite voxel query evidence](../pr-screenshots/codex/finite-voxel-shadow-query/README.md).

- [Finite defaults](finite-shadow-defaults.md) cover shape-only and non-main
  producer lifecycles; particle pipelines retain legacy casting.
- Preserve both the four GRID cubes and detached/attached entities in sweeps.
- Cardinal and per-axis voxel receivers use face-sample centers. Continuous
  fragment receiving and finite edge coverage crossing a display triangle remain
  separate work; do not fix them with a larger blur.
- OpenGL execution still requires a suitable host; shader changes are mirrored.
- Profile GPU stages and CPU update costs with repeated runs before optimizing.
