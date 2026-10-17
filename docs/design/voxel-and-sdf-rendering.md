# Choosing voxel sets and analytic shapes

## Representation and intended use

Both representations feed the trixel renderer. A compact shape descriptor is
not a promise of lower frame time: projected area, overlap, the SDF solver and
lighting work determine its cost. A voxel set is not a private canvas: shared
GRID batching and independently allocated DETACHED canvases have very different
memory and dispatch costs.

| Representation | Useful properties | Costs and limitations | Appropriate starting point |
|---|---|---|---|
| Shared GRID voxel sets | Arbitrary authored occupancy, edits, pooled geometry, exposed-face selection and chunk visibility | Occupancy storage; rebuilding rotated occupancy; subdivision and non-cardinal face coverage | Large populated worlds and editable voxel content |
| Plain DETACHED voxel sets | Continuous projected source faces under model/camera rotation | Per-canvas resources, face records and sorting; approximate face-flat AO; finite-face world casting uses source geometry; shadow reception samples once per source face | Rotating rigid voxel objects when their supported placement/lighting contract fits |
| Revoxelized DETACHED sets | Rotated occupancy rebuilt into connected local voxel faces | Revoxelization, private storage and real staircase geometry; approximation differs from continuous source faces | Objects intentionally requiring a voxelized rotated appearance |
| Analytic SDF shapes | Small descriptors, procedural primitives, analytic intersections for supported shapes, no authored occupancy allocation | Work scales with dispatched screen tiles and overlap; general/rotated solvers can search along depth; casting and display must agree | Tools, debug geometry and measured primitive-heavy scenes |

Retain both SDF and voxel APIs for now. Their authoring and storage roles are
useful, but they must not require developers to guess different dimensions,
placement or lighting behavior. Removal requires a consumer audit and an explicit
API decision; a local demo is not evidence that external users do not need one.

## SDF authoring with voxel runtime geometry

Lit game objects should converge on ordinary voxel geometry. An SDF can be an
authoring recipe without being a second runtime lighting representation.
`IRPrefab::Voxel::bakeSdfGrid` produces a DENSE asset that can be saved with the
existing asset writer or attached through `DenseVoxel::toComponent`. Rendering,
sun casting, reception, AO and local-light blocking then consume the same voxel
data as hand-authored occupancy. No shape-specific receiver solver is added by
this route. Direct SDF remains available for compatibility, tools, effects and
measured workloads; existing descriptors are not silently converted.

The initial baker handles solid built-in primitives. It has explicit grid bounds,
unit cells, opaque occupied records and the existing `distance <= 0.5` threshold.
It accepts descriptor-authored parameters and applies BOX `effectiveParams`
once; existing `fillSdf` callers retain their raw evaluation-parameter convention.
Cell centers are `i - (size - 1)/2`, so odd and even grids have different phases.
A BOX sample satisfies `2*abs(center) <= authored dimension`; boundary ties can
include an additional cell when grid and shape parity differ. The grid clips the
result. It must be sized intentionally, not inferred from a bounding radius and
assumed complete. CENTER/GROUND anchors and model transforms are applied when the
asset becomes an entity, not baked twice. Hollow flags, procedural color flags
and custom shader SDFs are not implicit conversion inputs.

The optional `maxWorkingBytes` argument defaults to 256 MiB and caps the
combined voxel-record and temporary distance-grid payload before either is
allocated. It excludes allocator overhead, existing caller-owned assets and later
pool/GPU uploads; it is not a process RSS limit. A caller can raise or lower this
authoring budget independently of grid dimensions. The integer cell-index limit
still applies, and a zero budget rejects every nonempty grid. Invalid extents,
nonfinite parameters, unsupported shape types and over-budget requests return an
empty asset; an actual allocation failure propagates normally. Supported built-ins
are explicitly enumerated, so a new enum value cannot silently use the evaluator's
fallback box. There is no 64-cubed bake limit.

The resulting finite cubes are the geometry of the object, including its shadow.
This is not a promise of the smooth analytic silhouette: direct rendering changes
its field threshold with subdivision, while a baked asset keeps its authored
occupancy. Finer authored grids and LODs require an explicit world-scale contract.
Do not blur or inflate the voxel shadow to resemble the analytic arm.

`IRShapeDebug --sdf-voxel-scene direct|baked` compares the same four primitive
parameters, placements and materials on an ordinary voxel floor with a local
light. `--sdf-voxel-blockers` adds ordinary voxel casters above the subjects;
`--no-shadows`, `--no-ao` and debug overlays isolate the effects. Automatic shots
include cardinal/intermediate yaw and two zoom levels with a fixed origin pivot.
Live `--spin-yaw` is supported separately from those automatic shots. This is a representation
comparison, not an assertion that both images must match. The baker's unit tests
independently check occupancy, threshold ties, torus holes, serialization and
pool attachment (all six face-occlusion bits, active masks and AO contribution).

`--sdf-voxel-pose root|parented` adds a fixed tilted pose and fractional
translation. The parented arm composes a translated Z half-turn with an oblique
local rotation; the root arm uses the independently expanded half-turn result.
Both must display the same geometry and lighting within a representation.
The baked runs use GRID rotation (resampled occupancy), not rigid DETACHED
faces. Their shared six-shot reference set checks four cardinal views, a 45-degree
view and effective subdivisions 4/2 with external voxel casters and a local light.
This comparison checks transform-path equivalence, not absolute correctness of
every voxel step, local-light occlusion or projected shadow boundary.

Baking costs CPU time and dense storage once per authored asset; attaching still
allocates one pool span per entity. Sharing an asset does not yet imply shared
resident GPU geometry. Warm-frame comparisons must separate startup baking and
uploads, then measure geometry, shadow, AO and lighting stages and complete frame
time on both backends before choosing defaults. Dynamic fields that change every
frame need a separate GPU occupancy-generation design and cost comparison.
Further analytic receiver extensions are deferred while this route is evaluated.

## Shared geometric contract

World dimensions and transforms are independent of camera yaw, zoom and sample
density. Subdivision changes sampling, not the physical size of a BOX or its
shadow caster. A voxelized curved surface and an analytic curve can have different
silhouettes, while still agreeing on placement and declared size conventions.

The raster producer owns the storage layout. A consumer must use that producer's
actual density, origin, face basis and sample layout. A shape sharing a private
canvas must preserve the other producer's color, depth and entity ownership.
Canvas placement is applied once. Raw rectangular trixel data is a debug view
for voxel presentation, not a substitute for reconstructed voxel faces. See the
[source geometry and parity gates](trixel-face-reconstruction-validation.md).

A visible face, its reconstructed receiver position and its light-space caster
must describe the same geometry. Sharing projection/depth helpers is preferable
to per-mode correction constants. SDF casting needs light-facing geometry rather
than only the camera-visible samples. Boxes and supported smooth curved shapes
intersect rays in light space; other SDF caster coverage remains a separate
validation target.

### Analytic sun casters

Smooth sphere, cylinder, cone and torus shapes cast their rendered field solid
(`evaluateSDF <= 0.5 / effectiveSubdivisions`) rather than camera-visible depth
points. Their world center uses the display projection's placement convention,
and model rotation uses the display's near-identity threshold. Lattice and hollow
shapes retain their sampled geometry; replacing those silhouettes with a smooth
solid would change the representation.

The curved caster and finite-face caster share the bounded sun-tile index. A
record contains either a finite quad or an axisymmetric solid's center, axis,
parameters and field threshold. Eligible surface receivers intersect the indexed
geometry at their continuous receiver position, so neither blur nor footprint
dilation is needed to connect the rim. Ray solving is algebraic for spheres and
quadratic bodies. Torus roots use derivative-partitioned, bounded bisection;
cylinder/cone cap pieces reproduce the field's threshold region. A fixed-step
SDF marcher is not sufficient for shallow cones or near-tangent thin rims.

The index retains 65,536 records across two 1024×1024 cascades. Each 8×8 sun tile
has 64 inline candidates; a shared spill pool extends complete queries to 256.
Typed records occupy ten words (40 bytes); production queries never scan the
whole shape list. [Capacity and publication rules](sun-face-index-capacity.md)
define when exhausted tiles use the separately baked light-space depth fallback.
That fallback has shadow-map sampling resolution;
an exact edge is only claimed for complete indexed tiles and eligible surface
receivers. Main lattice receivers and curved SDF receiver reconstruction remain
distinct validation targets. Casting a correct curve does not certify its own
display normals or self-shadow reconstruction.

`test_render_sdf_ray_roots.py` executes both shader implementations against
independent solid/ray fixtures, including rotated record queries, tangencies,
torus holes and shallow cones. `test_render_shape_sun_routing.py` checks mode
eligibility and the shared rotation convention. The footprint metric's
`--representation analytic` compares isolated floor shadows with a convergent
horizontal-section projection; its voxel mode instead projects occupied cells.
Both account for the Z1 framebuffer origin and exclude pixels hidden by the
caster using a matching no-shadow control. A one-pixel boundary band is a
measurement tolerance, not renderer smoothing.

### Retained curved shadow receivers

Smooth sphere and cylinder receivers recover the camera ray of the
retained producer sample, before its six display offsets. The ray intersects
`evaluateSDF <= 0.5 / effectiveSubdivisions`, the same solid used for casting.
The shared rendered center and rounded projected origin define placement;
cylinder rays also apply the producer's inverse model rotation. A local
AABB bounds the existing algebraic/partitioned solver. The earliest hit supplies
the world shadow position; the displayed signed face normal still supplies the
sun-facing test. Replacing that normal with an analytic gradient could skip
self-occlusion on a displayed face that faces the light from the solid's back side.

This computes shadow visibility per occupied canvas receiver texel, not per
framebuffer fragment. Display texels sharing a retained producer sample share
the same ray but can repeat the query; there is no per-owner result cache.
It preserves owner, depth, coverage, face-slot normals and material lighting. Hollow, lattice, subdivision-one and residual-deformed cardinal
shapes keep their existing path. The rounded cylinder cap solver is bounded but can invoke torus root queries;
native stage timings are required before calling this extension performance-neutral.

`test_render_curved_shadow_receiver.py` executes both shader receivers against
independent double-precision field bracketing and exact cap rays,
including model rotation, snapped placement, camera yaw, density and owner-key
halves. The sphere route controls separately preserve normal and provenance
gating. A deterministic rotated-torus control forbids moving a retained sample
onto a skipped nearer ray interval. These tests do not certify curved display reconstruction: accepted
fractional SDF samples are not a single voxel occupancy lattice, so their
finite-cube union is a representation comparison rather than a required surface.
Cone and torus receivers remain on their existing fallback. The producer keeps
its first accepted integer-depth sample; the first continuous root can belong
to a nearer interval containing no such sample. The torus is nonconvex, and the
cone's positive-threshold field is also locally nonconvex where its linear side
meets the spherical apex cap. Supporting these shapes requires recovering the
retained sample depth and selecting the entry of its connected occupied interval,
not merely the first root or a root in the preceding integer step. That work
must retain native owner/depth controls and bounded query cost.

The [torus ownership probe](../pr-screenshots/codex/shape-shadow-sun-space/torus-surface-probe/README.md)
retains the unresolved face-coverage evidence. An explicit finite-face display
contract must precede changes to those bands; normal averaging is not a repair.

Ambient occlusion is local ambient visibility, separate from sun visibility and
Lambert shading. A rotated voxel staircase has actual exposed faces and can have
actual concave contacts. Do not erase those normals or darken every face solely
because it is an X/Y/Z face. The quantized step of a tilted-flat surface is the
exception: locally identical to a crease, it is told apart only by the surface
returning to the receiver's own face one cell beyond the step, and that
resample is what keeps a rotated solid from reading as venetian-blind banding
(`scripts/render-ao-staircase-metric.py` measures it). Screen-space AO cannot
see hidden or off-screen occluders and should not be described as complete
geometric visibility.

## Comparable performance targets

The long-term target is a large simulated world with visibility-bounded rendering,
including a million-entity workload where practical. No million-visible-entity
60 FPS result has been established. A million private canvases, a million hidden
entities and a million pooled visible cells are different experiments.

At 60 FPS the complete frame budget is 16.67 ms. Report measured CPU and GPU stage
costs, frame-time percentiles and memory on a named host/backend; do not assign
representation budgets from descriptor size alone. Keep simulation cadence and
render density independently measurable.

Compare representations with matched world bounds, on-screen area, material,
visibility and lighting settings. Sweep entity count, overlap, yaw (cardinal and
intermediate), zoom and effective subdivisions separately. Include warm-up and a
static repeat control. Record:

- CPU gathering, culling, tile construction, uploads and submission.
- GPU geometry/revoxelization, sorting, AO, shadow baking, lighting and composition.
- Resident voxel/face/canvas memory and allocation churn.
- Visible entities/cells, dispatched tiles, actual private-canvas density and
  shadow casters, rather than only total world entity count.

GPU timings need stage attribution and end-to-end frame confirmation; CPU
submission time is not GPU execution time. The
[stage timing cost model](gpu-stage-timing-cost-model.md) describes the profiler.
Choose the representation from this measured workload and its geometric needs.

## Current acceptance work

These are open correctness gates, not approved visual baselines:

1. Remove the hard AO contact bands while retaining real concave contact shading
   and checking rotated GRID/revoxelized occupancy. Isolated ShapeDebug runs show
   the lower cube band persists without sun shadows and disappears without AO.
2. Mixed private SDF/voxel canvas density, local placement, owner rotation and
   atomic-depth lifecycle are gated
   ([mixed private canvas lifecycle](mixed-private-canvas-lifecycle.md)); the
   SDF marker's own display in a private canvas (raw texels at the capped
   density, dilation under continuous yaw) stays open under item 3.
3. Align SDF BOX display extent, receiver reconstruction and analytic caster
   extent across subdivisions and camera rotation. ShapeDebug currently shows a
   smaller SDF box beside its seven-voxel counterpart. Changing only the extent
   subtraction overshoots because dense SDF queries also stamp a 2x3 footprint.
4. Audit SDF surface face ownership/fragment reconstruction separately from plain
   DETACHED source faces. The latter's merged fixes do not certify SDF curves or
   revoxelized occupancy.
5. Extend independent geometry/depth/attachment gates, execute OpenGL validation,
   then profile the corrected paths before further performance restructuring.
