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

## Cardinal presentation receivers

Eligible main-world cardinal voxels retain albedo through the lighting compute
pass, then use the same `worldSurfaceLighting` composition and finite shadow
query as continuous per-axis faces. The gather reconstructs a continuous point
with `cardinalVoxelSurfacePoint`, rather than displaying one shadow sample across
both stored halves. The quad and its depth/identity sampling stay unchanged.

For a fragment's canvas coordinate, subtract the producer frame offset and the
one-texel storage-origin shift, then divide by effective density. Invert that
iso coordinate at depth zero to obtain a view ray point `p`. Let `c` be the
existing exact face-sample center and `a` the slot's unsigned unit axis. The
intersection is `p + dot(c-p,a) * (1,1,1)`, followed by inverse cardinal rotation.
Adding the same scalar to XYZ preserves iso projection and places the point on
the stored plane. Polarity affects the normal and stored plane, not the ray.

Rectangular storage can select the point beyond the owning micro-square. Each
in-plane axis `e` of the slot is then limited independently. With
`s = dot(point - c, e)` in the cardinal-0 frame and half extent `0.5 / density`,
a point with `|s|` within the half extent (plus `1e-4`) stays. Otherwise the
gather reads the texel one micro-step along `sign(s) * e` from the owner texel.
That step is the iso image of `e` (`(-1,-1)`, `(1,-1)` or `(0,2)`), so it keeps
the owner's half of the texel pair. The neighbour is coplanar when it stores the
same slot and flip at depth `owner ± 1` and carries the cardinal-voxel marker.
Across a coplanar neighbour the extrapolation stays, so a flat run remains one
continuous receiver. Across anything else (another face, an overwritten texel,
off-canvas), `s` is clamped to the half extent on that axis. Without the limit,
an edge fragment's point lands at or behind the neighbouring voxel, and the
finite query reads it as occluded by that voxel. The result is one-trixel
shadow islands along concave and convex edges. The diagonal neighbour is not
probed. The probe costs one distance read and one entity-id read per
overshooting axis, on the overshoot branch only. This extrapolation is a
receiver raster contract, not permission to extrapolate finite caster
footprints.

Against centre-sampled references the continuous receiver changes two classes
of pixel. Edge islands (shadowed pixels on a face boundary) come from an
unlimited point behind the neighbouring voxel, and the clamp arm removes them.
The remaining changes are sample-point changes, not defects. The texel pair
once displayed one centre sample. Each fragment now shows its own point, so a
shadow boundary inside a micro-square lights or darkens part of the pair. A
true shadow tip can therefore be one trixel wide.

Validators: `scripts/tests/test_render_cardinal_voxel_receiver.py` runs both
backends' receiver in a C++ adapter against staged neighbour texels. The
coplanar arm keeps the ray/plane intersection. The absent, wrong-slot,
wrong-flip, one-plane-off, no-marker and off-canvas arms clamp. Mutants for an
unconditional clamp, an unconditional extrapolation, a probe of the opposite
neighbour, and a coplanarity test that ignores slot, flip, depth, provenance or
the canvas bounds all fail. `scripts/render-shadow-speckle-metric.py` counts
reference-free edge speckles. The IRCanvasStress manifest gates its five GRID
mode-switch, detach and impulse rows on per-row ceilings, set to the counts the
offline CPU oracle confirmed as true shadow
(`docs/pr-screenshots/claude/4763-canvas-stress-mismatch-attribution/tools/receiver_oracle.py`).

The producer publishes readiness only after the main canvas tick. Consumer
resources alone cannot establish that color is albedo. Both stages require the
cardinal-voxel carrier and exclude cut faces, which retain their fully lit
cross-section behavior. Missing lighting resources, debug/depth views and fog
retain compute lighting. An active cardinal trixel-glitch route instead
publishes source-lit input beside the preserved albedo: affected pixels bypass
destination relighting, while unaffected pixels keep this continuous receiver
path. Secondary, detached, and shape-owned pixels keep their existing routes. A
combined shader handles mixed shape/voxel canvases; a voxel-only specialization
needs no retained shape owners. The ordinary route adds no texture, map capacity,
blur, or subdivision; the glitch presentation planes are allocated lazily.

`test_render_cardinal_voxel_receiver.py` checks points on and beyond each
micro-square against independent projected cube-face vertices, across densities,
cardinals, both halves, and polarities. Phase, plane, and micro-square-clamping
mutations fail. `test_render_cardinal_surface_lighting.py` executes the production
handoff and shader gates, including missing ticks, cut faces, disabled lighting,
and mixed shader selection. Native evidence and its limitations are recorded in
[cardinal presentation captures](../pr-screenshots/codex/cardinal-surface-shadow/README.md).

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

## Rounded finite-face boundaries

Finite face queries use `projectedFaceContainsClosed` from the shared projected
face helpers. Strict interior hits retain their fast path. For a rejected hit,
the four oriented edge numerators admit binary32 rounding uncertainty instead
of treating a slightly negative edge value as an exact miss. This closes the
reproduced interior floor-shadow hole in the SDF-authored voxel fixture without
blurring or changing its occupancy.

For the U coordinate, let `N = cross(P-C,V)` and `D = cross(U,V)`. The edge
tests are `sign(D)*N >= -E` and `sign(D)*(D-N) >= -E`, where
`E = gamma(7) * ((abs(P.x)+abs(C.x))*abs(V.y) +
(abs(P.y)+abs(C.y))*abs(V.x) + abs(U.x*V.y)+abs(U.y*V.x))`.
The V test swaps the basis axes. `gamma(7)=7u/(1-7u)`, with binary32 unit
roundoff `u=2^-24`, conservatively covers independently once-rounded projected
inputs and the edge arithmetic under finite normal operations. It is not a
bound on cancellation in upstream world-to-sun projection. Nearly grazing
faces can have large coordinate uncertainty, so accepted U/V coordinates are
clamped to the finite face before evaluating depth; they cannot extrapolate
an unbounded caster plane.

`test_render_projected_face_boundary.py` runs both production shader queries
against independently rounded double-precision parallelogram points: 20,952
boundary, exterior and grazing checks per backend, with strict-edge, inflated
footprint and separate U/V depth-extrapolation mutations. This certifies the
bounded predicate contract, not every projection or every candidate-list entry.
Candidate AABB seams at tile boundaries and exact classification inside a
grazing face's numerical uncertainty remain separate questions. The additional
arithmetic runs on candidate misses; profile rejection-heavy 64/256-candidate
tiles before claiming neutral GPU cost.

## Remaining work

Finite voxel casters of all three bases (world GRID, camera-aligned resampled,
and rigid source) also retain their actual projected face quads in the bounded
query index. A complete tile uses ray/quad intersections at the receiver point;
its indexed map layer is ignored after an exact miss. Eligible analytical
sphere, cylinder, cone and torus casters also publish typed finite records;
other casters stay in the independent primary layer. Incomplete tiles use the
indexed fallback layer, which preserves world/camera face markers for the plane-agreement calculation
above. Raster-origin receivers test each depth layer independently before
combining occlusion, so an out-of-range depth in one cannot erase the other.

Only faces overlapping at least one cascade consume records. All voxel bases
share the [bounded index](sun-face-index-capacity.md): 65,536 records, 64 inline
candidates per tile and up to 256 candidates with shared spill storage. Added
GRID or resampled geometry can still exhaust capacity and force previously exact
rigid-source tiles into approximate fallback; sharp edges at arbitrary density
are not guaranteed.
Native sparse-scene acceptance and outstanding limits are recorded in the
[finite voxel query evidence](../pr-screenshots/codex/finite-voxel-shadow-query/README.md).

- [Finite defaults](finite-shadow-defaults.md) cover shape-only and non-main
  producer lifecycles; particle pipelines retain legacy casting.
- Preserve both the four GRID cubes and detached/attached entities in sweeps.
- Cardinal fog pipelines and detached local triangles still use sampled
  receiver lighting. Unify their presentation composition without blurring or
  changing the geometry; eligible cardinal, trixel-glitch, and per-axis
  presentation receivers already use continuous surface points.
- OpenGL execution still requires a suitable host; shader changes are mirrored.
- Profile GPU stages and CPU update costs with repeated runs before optimizing.
