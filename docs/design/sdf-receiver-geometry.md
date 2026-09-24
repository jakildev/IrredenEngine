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
trigger that workflow, which requires a C++ compiler and an unskipped SDF
suite. This validates the intersection helpers, not production shadow or
presentation correctness.

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
for exact analytical recovery. The
[receiver/query factorial](../pr-screenshots/codex/sdf-receiver-factorial/README.md)
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
3. Retain receiver data until its last consumer. Shape descriptor uploads now
   belong to their canvas together with the producer's projection snapshot.
   The retained elected sample key addresses the retained tile lookup, whose
   shape index identifies its descriptor. Later overpainting conservatively invalidates the whole canvas;
   per-texel validity remains pending. Prefer storage tied
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

Future geometry work must preserve the SDF fog whole-body carrier from
`SHAPE_FLAG_FOG_WHOLE_BODY_EXEMPT` through `encodeEntityIdFogWholeBody`.
This is a correctness prerequisite, not a claim that the remaining floor
regression or finite shadow-map coverage is fixed.

## Opaque sample ownership

The SDF producer resolves depth, then elects the lowest submitted sample key
among samples at that depth, then publishes color and identity from that one
sample. Keys include tile, local invocation, face and half-face; changing GPU
scheduling cannot choose a different writer for an unchanged submission.
Descriptor reordering may change the chosen tie winner. Each shape canvas retains its owner buffer and the submitted tile lookup.
The owner buffer is cleared before that canvas’s election and costs four bytes
per pixel of its largest submitted size; aggregate storage is the sum over
shape canvases, not only the largest canvas. It does not grow directly with
the world entity population. Buffer-update and
shader-storage barriers separate clear, election and publication.

The elected sample key, tile lookup and descriptor array now survive other
canvas submissions. This retains the SDF pass winner, not final surface validity
after another producer overpaints the canvas. The existing occluded X-ray color overlay remains outside the
opaque ownership guarantee. Its read/modify/write blending is a separate
ordering problem. Native controls and validation live in the
[ownership evidence](../pr-screenshots/codex/sdf-winner-ownership/README.md).

## Fragment integration still pending

The continuous interval helper does not by itself provide a fragment receiver.
A finite descriptor-backed query is required at box corners; extrapolating one
sample's infinite plane can cross onto the wrong face. Retained per-canvas
descriptors must reproduce the producer's rounded projected origin, density and
cell expansion. Canvas-owned descriptor storage now survives other canvas uploads;
elected sample references survive too, but per-texel final-winner validity and fragment consumer
bindings remain to be implemented. The compute-shadow consumer below uses the
conservative whole-canvas validity contract.
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
receiving only where the finite intersection succeeds. The lifecycle is guarded conservatively by whole-canvas invalidation below;
finite per-fragment queries and coverage remain unimplemented. The shared
helper and scalar checks do not accept floor edges.

## Canvas-owned descriptor uploads

`CanvasShapeGeometry` retains the exact descriptor array submitted to a canvas
and its `GPUShapesFrameData` snapshot, including camera offset, density, yaw,
lattice mode and deformation. Submission uses that same buffer for depth,
election, publication and casting. Allocation grows to the next power of two
of the submitted count and is reused until growth or canvas destruction.
Canvases without a submitted tile allocate nothing. `SHAPES_TO_TRIXEL::beginTick`
resets active counts even if no shapes match; retained allocation is not a live
submission when its count is zero.

Storage is 80 bytes per descriptor capacity, bounded by the existing 8192-shape
submission cap per canvas. It replaces the shared fixed 655,360-byte upload,
not an additional copy. Multiple canvases can collectively exceed that former
fixed allocation; the tradeoff is stable per-canvas lifetime. No full-resolution
surface texture, GPU readback or extra dispatch is introduced. The 128-byte CPU
projection snapshot is retained per canvas, not per entity or trixel.

`scripts/tests/test_render_canvas_shape_geometry.py` executes the production
resource owner against checked in-memory resource operations. It tests two
independent canvases, snapshot retention, growth, reuse, empty uploads, invalid
counts and release; mutations must expose stale counts, lost snapshots and
leaked growth allocations. It does not substitute for native GPU validation.

These descriptors are producer inputs, not winning surface metadata. Canvas
clears, later same-depth overpainting, unsupported shapes and X-ray blending
still need explicit validity handling when winner references are added. No
fragment consumes this storage yet, and no visual shadow-edge fix is claimed.


## Retained elected sample references

For an occupied SDF owner texel, the existing election key encodes

```
key = ((tileIndex * 64 + localInvocation) * 3 + face) * 2 + halfFace
halfFace = key % 2
face = (key / 2) % 3
localInvocation = (key / 6) % 64
tileIndex = key / 384
shapeIndex = tiles[tileIndex].shapeIndex
```

`0xffffffff` means no elected shape sample. `localInvocation` is row-major
in the 8×8 tile. Tile iso origin plus that local offset identifies the producer
query; the displayed face slot is not an analytical surface normal. A padded
dispatch tile has `shapeIndex == -1` and cannot elect a sample.

`CanvasShapeGeometry` owns descriptors, padded tile descriptors and the owner
buffer. The same owner buffer feeds election and publication; there is no
additional copy, shader store, dispatch or readback. Owner extent and tile count
reset with the frame snapshot even on an empty frame. Reusing an equal-area
canvas with a different aspect ratio updates the owner stride and clears its
active bytes. Capacity remains resident until growth or canvas destruction.

The tile allocation is 16 bytes times its power-of-two padded-count capacity,
replacing the shared fixed 4 MiB tile upload. Owner storage changes from one
largest-canvas scratch buffer to the sum of per-shape-canvas high-water sizes.
For example, a 2560×1440 *trixel canvas* retains about 14.1 MiB of owner data;
framebuffer resolution alone does not determine this cost. Voxel-only canvases
allocate none. This is a lifetime tradeoff, not a performance improvement.

The retained reference is valid for the SDF submission only. Later canvas clears,
voxel/text/widget/particle writes, same-depth overwrites and X-ray color mixing
must be accounted for before a shadow or lighting consumer uses it. An entity-ID
or quantized-depth equality test alone is insufficient to certify a same-entity,
same-depth overwrite. The compute-shadow consumer below performs a finite box query. Fragment queries
and linear lighting payload remain unimplemented; retention alone does not improve shadow edges.


## Conservative final-write validity

`CanvasShapeGeometry::samplesValid()` is false until the SDF producer publishes
its completed submission. Uploads, owner preparation and frame reset invalidate
it first. Publication with any X-ray descriptor leaves it false, because mixed
color cannot be attributed to the elected opaque surface alone. The publication
assert checks populated submission fields; pipeline ordering, not that assert,
establishes that the GPU publication dispatch has been encoded.

Engine geometry writers acquire color through
`C_TriangleCanvasTextures::getTextureColorsForGeometryWrite()`. This invalidates
retained sample references before geometry/depth replacement, including a
same-entity, same-depth overwrite. CPU trixel/rectangle/mask painting, text and
widget glyphs, voxel rasterization, both particle paths and canvas clears use
this contract. A background-throttled clear still invalidates when depth clears.
Lighting and fog preserve geometric ownership and keep the ordinary getter.
Foreign GUI canvas writers declare their CPU canvas writes in system metadata.

Invalidation is whole-canvas and costs a boolean store, with no GPU dispatch,
readback, upload or additional per-texel allocation. Buffers remain resident for
reuse and diagnosis. Const canvas clear APIs can mutate GPU content, so the
matching derived validity bit is mutable as well. Re-validating requires a new
trusted SDF publication; readers must not infer validity from nonempty retained
buffers or matching entity IDs/depth alone.

This is conservative: a particle dispatch that changes no visible samples,
one overwritten corner, or an X-ray descriptor that never becomes visible
still disables the whole canvas’s exact-SDF eligibility. Per-texel invalidation
is pending. Custom renderers using raw texture handles must invalidate before
replacing geometry; the public raw resource API cannot enforce this automatically.
The compute-shadow pass consumes this state for eligible main-canvas boxes.
Presentation does not consume it, and linear lighting payload remains unimplemented.

The resource-owner test executes validity transitions and mutations that accept
X-ray publication or skip invalidation. Writer coverage is additionally pinned
in source; it does not execute every producer or prove GPU completion.


## Finite box receivers in the compute-shadow pass

`COMPUTE_SUN_SHADOW` binds the main canvas's valid descriptors, elected owners,
tile lookup and projection snapshot. The elected key selects one tile and one
descriptor; no shape scan is performed. `shapeBoxReceiver` queries that finite
box at the current canvas sample coordinate, retaining continuous surface depth
and its signed world normal for the existing shadow sampler's bias calculation.
It reproduces producer center rounding, effective density and half-cell expansion.
Its inverse projection uses `isoPositionToPos3D`, shared with the other render paths.

Eligibility excludes hollow and entity-rotated boxes, other primitive types,
lattice rasterization, and residual-only face deformation. Private/model-space
canvases and per-axis voxel canvases keep their existing reconstruction. A miss
retains the original sampled receiver: finite geometry never discards existing
splat coverage or changes stored color, depth or entity identity. Invalidated
canvases also retain the original route. Actual fragment coordinates and curved/rotated analytical receivers remain pending.

The CPU selects a compile-time specialized kernel for eligible canvases. The
ordinary kernel and all per-axis dispatches exclude the receiver query and its
buffer declarations entirely; they do not pay for a disabled uniform branch.
Both kernels compile from one shared body. Preprocessing controls prove the
ordinary GLSL and Metal variants omit receiver code, with inverted wrapper
macros as positive controls.

This adds a 128-byte per-system projection upload for eligible canvases and an
80-byte durable dummy binding allocation. The existing per-canvas buffers are read directly without
copying, a new image, a readback or an additional GPU pass. Borrowed animation and
shape-frame slots are restored; canvas-owned descriptor/tile bindings are replaced
with the durable dummy before a canvas can be destroyed. GPU cost has not yet
been profiled; this is a correctness change, not a performance claim.

`test_render_shape_receiver.py` executes the actual query on both backends against
an independent double-precision ray oracle: 377,920 finite hits and 235,392 misses
per backend, all yaw quadrants, fractional centers/camera offsets and densities
1/2/4/8. It also executes the compute shader's selected-owner block with checked
buffer adapters, testing nonzero tiles/descriptors, no owner, disabled/per-axis
routes and finite misses. Mutations break density, cell expansion, fractional
queries, lattice rejection, tile decoding, sentinel and fallback controls.
Layout checks compare producer and receiver declarations. These scalar adapters
do not replace native backend execution or certify final displayed shadow edges.

Native evidence and exact shadows-disabled controls are in
[the finite receiver captures](../pr-screenshots/codex/sdf-box-shadow-receiver/README.md).
The one-value-per-trixel output still cannot express a shadow boundary within
one displayed triangle. The captured outlines remain jagged; this is not acceptance
of the final sharp-shadow objective and no blur was added.

## Exact box normals in lighting

The shadow texture remains RGBA8: R holds visibility, A holds `(FaceId + 1) / 255`
for the six signed world-axis normals, and A=0 selects the legacy receiver.
Only a successful finite box query writes a face code. Empty pixels, unsupported
shapes and finite misses write zero. The specialized producer computes this code
even with shadows disabled, keeping Lambert/sky lighting independent of the shadow toggle.
This exact encoding is restricted to axis-aligned analytical boxes; general curved
or rotated normals need a different representation.

The main canvas lighting variant decodes the retained face before normal debug,
Lambert and sky terms. CPU validity selects the variant each canvas tick, and the
ordinary program is restored before per-axis dispatch. Both variants share a body;
ordinary kernels exclude the carrier read and helper at preprocessing time.
No additional texture, dispatch or allocation is introduced. Position-dependent
local lights still use the legacy reconstructed position.

The receiver tests execute production producer/consumer assignments and UNORM8
roundtrips, with dropped carrier, consumer and sentinel mutations. Native evidence:
[box lighting normals](../pr-screenshots/codex/sdf-box-lighting-normal/README.md).

## Linear lighting composition contract

`ir_surface_lighting` is shared by canvas/overflow compute lighting and source-face
fragment composition. `surfaceSunFactor` attenuates only the directional term;
ambient survives zero sun visibility. `surfaceDisplayColor` applies exposure and
ACES only after linear contributions are composed (or clamps the non-HDR result).
Deferred source faces keep the untone-mapped base and direct-sun contribution until
the fragment samples visibility. The helpers add no new geometry or payload storage.

`test_render_source_face_lighting.py` executes both backend implementations with
11,979 ambient/intensity/Lambert/visibility combinations against a double-precision
oracle, plus deferred composition, HDR, debug and alpha checks. Mutations darken
ambient, tone-map before visibility, or overwrite alpha and must fail.

[Native equivalence captures](../pr-screenshots/codex/surface-lighting-contract/README.md)
cover boxes and mixed per-axis geometry; a source-face overlapping-caster probe
exercises native fragment composition. Analytical SDF fragment payloads remain pending.


### Sky hemisphere

`surfaceSkyLight` shares the sky response between canvas and overflow lighting
on both backends: `skyColor * intensity * max(-worldNormal.z, 0) * ao`.
World +Z points down. A top face has normal -Z; the underside +Z receives no
upper-hemisphere term. Yaw leaves that response unchanged, while object pitch
changes it continuously. Sky remains independent of direct sun visibility and
is composed in linear space before display mapping. This is an AO-modulated
hemisphere approximation, not geometry-traced sky visibility.

The finite SDF presentation follow-up must evaluate this term using its actual
fragment normal; storing only already-lit RGBA8 cannot recover that response.
See [native and deterministic controls](../pr-screenshots/codex/sky-hemisphere-normal/README.md).
