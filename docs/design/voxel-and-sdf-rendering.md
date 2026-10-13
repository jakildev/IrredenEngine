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
