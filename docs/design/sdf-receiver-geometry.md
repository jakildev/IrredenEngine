# SDF receiver geometry through the trixel pipeline

The smooth SDF receiver cannot recover its exact surface from the existing
integer depth and display slot alone. The display slot is not an analytical
surface normal. This matters for shadow comparisons and for geometric shadow
boundaries within a displayed trixel.

## Executable evidence

Run `python3 scripts/tests/test_render_sdf_surface_contract.py`. It extracts
`boxSurfaceIntervalYaw`, `boxSlabIntersectYaw`, `slabFromLinear`, `stableCeilToInt` and
`encodeDepthWithFace` from the actual GLSL and Metal sources and executes them
as scalar C++. An independent double-precision world-ray/slab intersection
checks hit/miss, entry/exit, signed entry normal, and whether the entry lies on the finite box.
The shared interval helper accepts continuous iso coordinates; the integer
producer forwards to it without changing its cell expansion or quantization.
Explicit corner controls require deterministic X, then Y, then Z tie ownership.

Each backend covers 480,000 rays: a full turn in 11.25° steps, offsets of
±0.00001 radians around each pose, positive/negative integer and fractional screen coordinates, and
densities 1, 2, 4 and 8. Both report 268,926 hits and 211,074 misses, with maximum
entry/exit depth error about 0.0000116. Both X/Y normal polarities and negative
Z are observed. Camera yaw alone does not expose positive Z; full object
rotation, curved surfaces, hollow shapes and GPU execution remain outside
this scalar test. There is no new image tolerance.

Positive controls reverse yaw polarity or a normal, round the continuous entry
depth or fragment coordinates, or accept an out-of-bounds parallel slab. Each altered shader fails the oracle.
The suite is discovered by the render-harness CI runner. Shader-only edits
trigger that workflow, which requires a C++ compiler and an unskipped SDF suite. This validates
the intersection helpers, not production shadow or presentation correctness.

## Why the stored value is insufficient

For screen coordinates `(i,j)` and view depth `d`, the view ray is

```
p(d) = (-i/2 - j/6, i/2 - j/6, j/3) + d * (1,1,1)/3
world(d) = Rz(yaw) * p(d)
```

The box interval solver retains continuous entry/exit depths. The producer's
subsequent integer depth loses the fractional entry. In the test, at screen
origin and yaw zero, two slab boxes have half extents

```
A = (density + 0.1, 5*density, 6*density)
B = (5*density, density + 0.2, 6*density)
```

Their entries are `-3*density - 0.3` and `-3*density - 0.6`, on negative X and
negative Y respectively. Both quantize to `-3*density`. For any common display
slot, the production encoder returns the same value. The test checks this at
all four densities. These extents are inputs to the slab helper; they are not
a proposal to change the SDF producer's existing half-cell boundary convention.

The alias is already present at the source sample, before the six-slot emission
spreads a common base depth over neighboring storage locations. A slot-derived
normal, a constant position offset, or a different shadow filter cannot invert
this many-to-one mapping. Additional winning-surface information is required
for exact analytical recovery. The [receiver/query factorial](../pr-screenshots/codex/sdf-receiver-factorial/README.md)
shows why changing receiver or sampler independently is insufficient in the
rendered floor fixture.

## Implementation sequence and constraints

1. Preserve surface provenance with the selected visible winner. Smooth SDF
   samples need a continuous surface description; voxelized SDF samples need
   their selected cell faces. Do not turn legitimate voxel steps into analytical
   surfaces. A descriptor reference can recover analytical geometry, while a
   planar surface can use a plane equation; the storage choice is still open.
2. Publish geometry from the same elected owner as color and identity. Opaque
   ownership is implemented below; independently raced normal/depth stores would
   still permit geometry from different winners to mix.
3. Retain receiver data until its last consumer. The current shape upload buffer
   is reused across canvases; a fragment cannot blindly refer to a previous
   canvas's descriptor index after that upload is replaced. Prefer storage tied
   to visible canvas winners over allocation proportional to world population,
   and never scan every shape per fragment.
4. Evaluate the receiver at the fragment's actual projected position. Carry
   finite shadow boundaries through presentation; one visibility value per
   trixel cannot express an edge that crosses its interior. Keep the selected
   caster representation and exact receiver geometry consistent.
5. Validate mixed modes, overlaps and depth ties, transformed canvases, all yaw
   quadrants, density changes, shadows disabled and resource reuse. Compare
   exact-ray controls with the unchanged strict image gates on both backends;
   measure visible-sample memory and GPU time before expanding the path.

The refreshed base includes PR #3629's SDF fog-carrier changes. The scalar gate
edits no producer or shared GPU structure and can land independently of the
deferred cascade correction. Future geometry work must preserve that carrier. This is a correctness prerequisite, not a claim
that the remaining floor regression or finite shadow-map coverage is fixed.

## Opaque sample ownership

The SDF producer resolves depth, then elects the lowest submitted sample key
among samples at that depth, then publishes color and identity from that one
sample. Keys include tile, local invocation, face and half-face; changing GPU
scheduling cannot choose a different writer for an unchanged submission.
Descriptor reordering may change the chosen tie winner. The reused owner buffer
is cleared per canvas and has four bytes per pixel of the largest processed
canvas; it does not grow with the world entity population. Buffer-update and
shader-storage barriers separate clear, election and publication.

This is the prerequisite for coherent surface metadata, not that metadata's
implementation. The existing occluded X-ray color overlay remains outside the
opaque ownership guarantee. Its read/modify/write blending is a separate
ordering problem. Native controls and validation live in the
[ownership evidence](../pr-screenshots/codex/sdf-winner-ownership/README.md).

## Fragment integration still pending

The continuous interval helper does not by itself provide a fragment receiver.
A finite descriptor-backed query is required at box corners; extrapolating one
sample's infinite plane can cross onto the wrong face. Retained per-canvas
descriptors must reproduce the producer's rounded projected origin, density and
cell expansion. The general shape upload cannot outlive its next canvas upload.
Voxelized/lattice SDF receivers retain cell-union geometry rather than one smooth
analytical box.

Lighting must retain linear ambient/indirect terms and an unlambertized material
sun coefficient. The fragment derives Lambert and visibility from its actual
surface before composition and tone mapping; the existing RGBA8 lit color cannot
be inverted to recover these terms. Normal-dependent sky lighting must follow
the same normal. Fog, AO and local-light semantics must remain explicit.

Winner metadata needs a lifecycle covering empty-SDF frames, canvas clears,
unsupported winners and later same-depth overwrites. A failed finite query must
not silently discard the producer's splatted coverage or change depth. The
initial integration should preserve that legacy coverage while accepting exact
receiving only where the finite intersection succeeds. These constraints remain
unimplemented; the shared helper and scalar checks do not accept floor edges.
