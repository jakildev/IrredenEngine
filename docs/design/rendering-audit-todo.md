# Rendering audit worklist

Finish visual correctness before the next optimization round. Then profile the
changed paths and other bottlenecks, consolidating logic and improving robustness
against measured costs and the visual controls. Keep the agreed work in this order. A diagnostic experiment is not an implemented
fix, and a small native scene does not establish fleet-scale rendering throughput.

## Current shadow investigation

- [Rigid SO(3) probes](../pr-screenshots/codex/rigid-voxel-rotation-probes/README.md)
  expose the existing continuous source-face path without revoxelizing. Fourteen
  single-voxel pose/camera checks pass; two nearly edge-on cases are inconclusive.
  [Multi-voxel occlusion controls](../pr-screenshots/codex/source-face-occlusion-oracle/README.md)
  pass all eight normal/visible-face checks but fail six of eight baseline shadow
  checks. Exact-ray fixture controls pass all eight; caster normals alone do not
  improve them. [Bounded finite source queries](../pr-screenshots/codex/finite-source-shadow-queries/README.md)
  now pass all eight shadow checks using actual transformed face footprints.
  Tile/record overflow retains approximate fallback; dense temporal transitions
  remain unverified. The mixed-source sampler regression now executes both
  backend surface loops with mutation controls; native tile/bake boundary
  coverage still needs a targeted fixture.
  [Rigid source casting](../pr-screenshots/codex/rigid-source-shadow-casters/README.md)
  now projects original transformed faces: 12 aggregate hull checks pass, while
  all 12 strict edge checks still fail. [Source-face reception](../pr-screenshots/codex/surface-shadow-receiver-controls/README.md)
  now samples continuous face centers; eight relative-position and four normal
  checks pass. The
  [continuous source-surface gate](../pr-screenshots/codex/continuous-source-shadow-oracle/README.md)
  rejects all eight prior face-center captures. The
  [fragment receiver implementation](../pr-screenshots/codex/continuous-source-face-lighting/README.md)
  now passes all eight with zero interior errors at unchanged tolerances. It
  interpolates original surface positions and combines direct visibility with
  separate linear lighting terms before tone mapping. Local-volume/sky/AO terms
  remain face-centered; dense overflow and temporal transitions still need coverage.
  Profile the 80-byte source records and per-fragment bounded queries before
  making any population-throughput claim.


- [Caster/receiver matrix](../pr-screenshots/codex/shadow-receiver-mode-matrix/README.md):
  16 mode pairs at eight yaws captured on Metal. The original matrix predates
  rigid source casting and receiving. Both now participate; intermediate GRID
  receivers show interior shadow gaps, and source/SDF contact controls retain
  boundary differences.
  Resolve those separately from silhouette aliasing. Add explicit participation
  and local-trixel versus continuous-surface sampling choices after their geometric
  contracts are validated; world placement and screen locking remain separate.


- [Caster/receiver plane agreement](surface-shadow-sampling.md) removes false
  shadow bands on the two resampled cubes at 45° and 135°. The strict eight-yaw
  comparison improves four views without increasing errors in the other twelve;
  remaining errors are explicitly retained in its evidence. This is not complete
  finite shadow coverage.
- [Strict floor-edge evidence](../pr-screenshots/codex/floor-shadow-plane-sampling/README.md)
  now catches all four cardinal failures missed by aggregate shadow IoU. The
  caster-plane PCF experiment was pixel-identical and rejected. Exact fixture
  visibility per fragment passes all quadrants; per-trixel visibility retains
  small edge failures. Fix map/receiver sampling and carry boundary geometry
  through presentation. The strict oracle now requires measured density and
  known projection instead of inferring precision from SDF floor bounds.
- [Receiver/query factorial controls](../pr-screenshots/codex/sdf-receiver-factorial/README.md)
  separate the known-floor experiment: receiver-only worsens all quadrants;
  query-only darkens almost the whole floor. Combined reduces total error but
  still fails all strict checks, with missed pixels at 180° rising from 19 to 48.
  Preserve winning surface location, normal and coverage together with the
  matching query contract; these hardcoded fixture patches are not a shipped fix.
- [Cascade receiver correction](https://github.com/jakildev/IrredenEngine/pull/3635)
  is deferred outside the merge-ready stack. Its axis derivation is sound, but
  the refreshed floor control still worsens at 180° from 12/266 to 19/356
  missing/excess pixels. The factorial captures use that experimental branch;
  they do not describe the current production baseline.

- [SDF surface contract](sdf-receiver-geometry.md): both shader interval solvers
  share continuous box intersection and signed normals, checked by an independent
  480,000-ray integer/fractional oracle per backend, but distinct planes
  alias to the same stored depth/slot. Preserve winning geometry through its
  last consumer; slot-derived normals cannot recover an analytical surface.
  This scalar gate does not accept the remaining rendered shadow failures.
- Next: resolve finite sampling misses and small GRID floor-shadow boundaries
  across quadrants against ray/face geometry, preserving legitimate partial faces.
- [Finite voxel queries](../pr-screenshots/codex/finite-voxel-shadow-query/README.md)
  retain projected GRID and revoxelized faces alongside rigid source faces.
  GRID casting onto a continuous source floor passes all four strict cardinal
  edge controls with zero errors. The [revoxelized oracle correction](../pr-screenshots/codex/revoxelized-shadow-oracle/README.md)
  accounts for its preserved lattice phase: cardinal captures at actual densities 1, 2 and 3
  pass all twelve strict controls, while pre-query captures still fail all four.
  Arbitrary revoxelized rotations and dense overflow remain unaccepted. SDF/GRID receivers still need
  continuous winning surface data through fragment presentation. Shared index
  capacity can reduce rigid-source exactness in dense scenes; test temporal
  overflow and larger populations before promoting this as a scalable solution.
- Keep six oriented face normals, twelve geometric half-faces, coordinate basis
  and screen parity distinct; see the [identity contract](trixel-face-reconstruction-validation.md#oriented-face-identity-versus-screen-parity).
- Default-scene context still contains visible banding/trixel artifacts in both
  parent and receiver-enabled captures. The independent frame and stepped
  octahedron oracle now isolates map-sampling failures from passing normal
  ownership and exact-ray controls. Resolve those failures before certifying
  self-shadows; preserve legitimate staircase occlusion.
- Then resume density/rotation GPU profiling and population scaling; native Metal
  correctness evidence does not establish OpenGL runtime parity or a frame-rate target.

## Agreed work

| Priority | Work | State / next acceptance |
|---|---|---|
| 1 | Detached face geometry and trixel display | Investigated in [display diagnosis](detached-trixel-display.md). Visible source faces, depth and picking still need a consistent projection. Origin and camera placement corrected. [Back-facing emission](detached-face-normals.md) now has a targeted correction and normal oracle; [Local triangular display](detached-local-triangles.md) is the normal undilated display with a per-face oracle; raw rectangular display is debug-only. Source-face reconstruction and depth/picking remain. |
| 2 | Shadow reception and contact | [Direct-sun occlusion response](sun-occlusion-response.md) now removes direct sunlight behind opaque blockers while retaining ambient. Paired self/external controls pass. Receivers still use reconstructed surfaces; [Staircase near-rejection](staircase-shadow-visibility.md) no longer erases close external blockers. Check remaining concave faces, false self-shadowing and contact against actual geometry. |
| 3 | Projected face boundaries | Reconstruct voxel-face coverage from light direction and receiver geometry. [Sun sample alignment](sun-sample-centers.md) now follows texel centers without adding filter taps. Clean edges must follow projected geometry, not blur, inflated coverage or bias that hides errors. |
| 4 | Duplicate CPU occupancy reconstruction | [Upload ownership](detached-mask-upload-path.md) now skips CPU reconstruction when inverse GPU resampling owns masks. Identity and buffer fallback retain reconstruction; ten native comparisons are unchanged. Measure population cost next. |
| 5 | Mode and scale validation | Extend density, screen-lock, pan, cascade, sparse/elongated asset and OpenGL coverage. Measure representative large populations and GPU cost before changing defaults. |

Attached probes and corrected authored probe placement are already in the stack.
The source-face shadow paths remain opt-in. Ready-for-review PRs do not imply
that the experimental rendering is ready to become the default.

## Proposed follow-ups discovered during this work

| Priority | Finding | Evidence / next check |
|---|---|---|
| High, within geometry | Detached centroid changes with camera yaw | Camera correction now uses framebuffer-pixel precision with Y-up placement; see [pan evidence](detached-camera-placement.md). Dilation asymmetry and resampled face geometry remain separate errors. |
| Medium, within geometry | Detached dilation expands the visible box | At yaw zero, zoom 2, its area is 1.219× the analytical box. The origin fix leaves that ratio unchanged. Preserve concavities when replacing dilation. |
| Medium, validation | Small-voxel and picking coverage | [Magnified single-voxel control](single-voxel-display-probe.md) now distinguishes detached failure from a 5/5 passing GRID control. Triangle-ID/face-color assertions and a picked-surface oracle remain. The detached composite explicitly disables hover readback, so picking needs implementation. |
| High, within scale validation | Detached composite has a 512-instance limit | `ENTITY_CANVAS_TO_FRAMEBUFFER` stops collecting after `kMaxEntityCanvasInstances`. Raising the limit alone does not meet the entity-count target; design and measure batching/culling for private canvases separately from attached GRID entities. |
| Medium, camera | Depth-derived pivot jumps during noncardinal pan | A zoom-16 isolated voxel jumps about 32 screenshot pixels as the default pivot changes. Verify with fixed-pivot controls; separate camera focus behavior from detached display. |
| Medium, validation | Density-dependent receiver calibration | The small SDF plate needs its smooth boundary convention accounted for at zoom 16. The new fixture does so; generalize the older large-box calibration before using it to judge other densities. |

## Newly observed during local-triangle validation

- The 225-degree staircase still has triangular teeth with normal fragment
  reconstruction and shadows disabled (analytic coexistence captures 610/612).
  Raw-debug capture 611 exactly reproduces historical rectangular capture 585.
  Resolve occupancy and face-boundary reconstruction; changing display defaults
  alone does not fix this geometry, and blur is not an acceptable substitute.

- Repeated identical upright captures differ in 104–952 pixels confined to the
  rainbow probe with triangle mode disabled. Investigate color/depth winner
  determinism before treating this probe as a strict pixel reference; the cause
  is not established. Retained comparisons: `codex/detached-local-triangles`.
- Actual private density 12 was exercised with the smallzoom fixture, but its
  high-density face coverage still needs a numerical oracle. Requested
  subdivisions alone must not stand in for measured effective density.

## Newly observed during occlusion validation

- [Regular demo coverage](canvas-stress-shadow-gaps.md) reproduces the user's
  four red/green/blue/yellow GRID cubes: default shadows have long interior
  gaps, while finite face casting closes them at the matching camera angle
  and at effective densities 1 and 4. This is still an opt-in path, not a
  default-path fix. Retain these cubes alongside detached revox coverage.
- The same investigation preserves a raster-phase/centroid experiment that
  removes unblocked false shadows at cardinal angles and actual density 2.
  It exposes a real thin-roof miss caused by the existing normal offset.
  Zero offset reduces the blocked error from 101 to 5 but still fails the
  unchanged tolerance of 2. Both patches are retained, not adopted. Resolve
  receiver location and finite boundary sampling together before promotion.
- [Finite analytic box casting](analytic-box-sun-coverage.md) now replaces BOX
  depth splats in the opt-in face-shadow path. Detached and GRID staircase
  outside controls both pass with zero error; rotated/fractional box hulls
  pass 4/4. The nearly hidden unrotated analytic box shadow remains below the
  unchanged IoU threshold (.668 versus .70); non-box shapes retain depth splats.
- Centroid sampling remains experimental: a broader unblocked sweep exposes
  source/raster half-cell disagreement (12,544 false-shadow pixels at yaw zero).
  Resampled face casting matches shadows-disabled exactly. The retained phase
  experiment corrects the cardinal case but exposes the nearby-blocker miss
  above. Check odd/even effective densities and fractional source bounds before
  adopting the combined correction.

- [Receiver sample positions](detached-shadow-receiver-samples.md): the current
  detached lookup fails all 30 triangle-centroid checks across five yaws. The
  derived correction passes all 30. Its original analytic-roof overcoverage
  (outside-patch error 41) is resolved by finite box casting; phase and nearby
  blocker sampling remain experimental as described above. Validate whole
  shadow boundaries before adoption.

- [Overhead direction controls](lighting-direction-probes.md) expose 3,808 false
  shadow pixels on unblocked GRID staircase treads with the default caster.
  Source-face casting matches shadows disabled exactly. [Footprint experiments](analytic-face-shadow-coexistence.md)
  establish that top-surface splats alone reproduce the error; radius zero removes
  it but fails box coverage. The default caster remains unfixed. Finite face
  footprints remain the intended correction, with no blur or normal averaging.

- [Analytic caster coexistence](analytic-face-shadow-coexistence.md) now preserves
  main-canvas SDF shadows alongside voxel/source-face casting. The detached roof
  control passes; the GRID outside patch retains an 8-level error and fails.
  Analytic finite coverage, non-main producers, and the 32 changed plate-edge
  pixels at three cardinal angles need validation before default adoption.
  Profile the added SDF depth pass and consolidate traversal in the optimization
  round after visual correctness.

- [Staircase visibility](staircase-shadow-visibility.md) is corrected: a nearby
  external blocker no longer loses its shadow at tread boundaries. The default
  depth caster still falsely shadows an outside control region, unlike full
  voxel-face casting. Resolve caster/receiver geometry agreement before treating
  the full lighting oracle as passing on that default path.
- The detached staircase at density 1 has alternating triangular shadow values
  on otherwise broad treads (capture 494 in the staircase visibility evidence).
  [Centered receiver recovery](detached-receiver-planes.md) removes the false
  pattern: four cardinal unblocked views now match shadows-disabled exactly,
  while the overhead shadow remains. Default depth casting and reconstructed
  face boundaries still need agreement with actual geometry. [Sample alignment](sun-sample-centers.md)
  also fixes the detached default-caster outside control; GRID retains an
  8-level error and still fails the unchanged 2-level tolerance.
- The unobstructed source-face plate has weak false self-shadowing at camera yaw
  90/270. Full direct visibility makes 1,048/16 pixels differ from the prior
  response, by at most 2/1 color levels. This is a receiver/caster agreement
  follow-up, not evidence that normals should be averaged or shadows blurred.
- Fully occluded direct sunlight previously retained 45% intensity, on top of
  ambient. That response is corrected; ambient is still a constant fill rather
  than physically traced indirect light. Local-light transport is a separate
  visibility audit.

## Origin correction evidence

The detached quad places the model origin by compensating for the canvas's
`(-1,-1)` texel origin offset. Texture Y increases down; framebuffer Y increases
up. Applying `(+texelX,-texelY)` instead of `(+texelX,+texelY)` removes the
two-texel vertical displacement without changing the depth key or caster geometry.
No buffer allocation, upload or draw is added.

The visible-box oracle projects the centered authored mass, with corners at
`±(halfCenterSpan + 0.5)`. It calibrates screen scale from the complete receiver
plate and uses no renderer depth samples. The shared projection hull/plate helpers
are also used by the existing shadow oracle; its thresholds and projection
conventions are unchanged in this slice.

At 2560×1440, yaw zero, zoom 2:

| Capture | Centroid error (pixels) | Visible area / expected | Placement |
|---|---:|---:|---|
| Original detached | (+0.5, -8.0) | 1.219 | Fail |
| Corrected detached | (+0.5, 0.0) | 1.219 | Pass |
| Attached GRID control | (+0.5, -1.0) | 1.002 | Pass |

`--placement-only` accepts at most two pixels of error per axis, allowing the
plate calibration/raster quantization. The default additionally requires silhouette
IoU ≥.95 and area ratio .95–1.05. All five GRID captures pass the default check;
the corrected detached silhouette still fails. These outcomes intentionally keep
the remaining geometry defect visible.

```sh
fleet-run --timeout 120 IRCanvasStress --only shadowbox,floor --no-spin --no-auto-rotate --no-ao --subdivisions 1 --zoom 2 --auto-screenshot 6 --sweep-yaw 0 1.57079633 5 --source-face-shadows
python3 scripts/render-visible-box-metric.py yaw0.png --placement-only
python3 scripts/render-visible-box-metric.py yaw45.png --yaw 45
```

Add `--probe-grid` to capture the attached control. Keep the complete plate in
view; the oracle is specific to this scene and does not validate arbitrary assets,
lighting, internal face boundaries or temporal stability.

Native Metal sweeps exited cleanly. Existing source-shadow checks remain 4/4 after
the placement change (yaw-zero IoU .712, the least margin). OpenGL runtime remains
unverified; the corrected C++ placement is shared by both backends.

Retained evidence: `docs/pr-screenshots/codex/visible-box-oracle/`. Capture 135
uses the original placement from baseline `a2c55707add8ba1a74d55091f17f203c92160084`;
125–129 use corrected detached placement at 0/22.5/45/67.5/90 degrees; 130–134
are the GRID controls at the same angles. Captures 136–140 are the corrected
four-probe scene. For example:

```sh
python3 scripts/render-visible-box-metric.py docs/pr-screenshots/codex/visible-box-oracle/capture-125.png --placement-only
python3 scripts/render-visible-box-metric.py docs/pr-screenshots/codex/visible-box-oracle/capture-132.png --yaw 45
```

## Centered shadow corners

The face-shadow kernel now subtracts `kVoxelRasterCellAnchor` from both source
and resampled cell positions before projecting corners. This aligns the occupied
mass with the visible renderer’s centered-voxel convention. The shadow oracle now
uses min(center)-0.5 and max(center)+0.5; its old lower-corner expectation was
part of the discrepancy, so earlier passes alone did not establish correctness.
No thresholds were relaxed. Neighbor occupancy, projected face size, dispatch
counts and buffer allocation are unchanged.

Native Metal box checks: source 4/4, GRID 4/4 and resampled detached 4/4. This
corrects the half-cell convention, not receiver reconstruction or large-scale
performance. Current captures 146–149 (source), 150–153 (GRID), 154–157
(resampled detached), and 158–162 (upright probes at 0/22.5/45/67.5/90 degrees)
are retained under `docs/pr-screenshots/codex/voxel-shadow-cell-anchor/`.

![Centered shadow corners: before and after](../pr-screenshots/codex/voxel-shadow-cell-anchor/upright-comparison.png)

The comparison crops the same rectangle from full frames; left is captures
136–140 (parent), right is 158–162. Face striping at 22.5 degrees and rectangular
coverage at 45 degrees remain visible. Baseline source boxes 141–144 also pass
the corrected oracle: IoU .710/.938/.918/.887, versus .716/.943/.913/.876
afterward. These aggregate thresholds are not a precise contact-alignment test;
the correction follows the independently checked centered-mass convention.

## Mixed-source sampler regression

`python3 -m unittest discover -s scripts/tests -p test_render_mixed_source_shadow.py`
executes the surface depth-layer loop extracted from each production shader. A
source quad covers the map tap but misses the receiver ray; an independent plane
behind it must still occlude. The controls also require an exact miss to suppress
the source fallback, incomplete queries to retain that fallback, empty/behind/far
planes to stay lit, and both cascade offsets to preserve layer ownership. Three
mutants per backend must fail their designated cases: discarding the other layer,
accepting source fallback after an exact miss, and dropping overflow fallback.
This closes the scalar sampler-selection gap from the previous native overlap
experiment, which did not fail its negative control. It does not exercise GPU
atomics, tile population, final raster coverage or arbitrary rotated normals.

Concurrent work checked before this slice: #3595 owns graded rim diagnostics and
shared shadow-mask classification; #3588 refreshes screenshot references; #3584
bounds voxel-pool capacity; #3577/#3581 own million-entity profiling controls;
#3597 repairs OpenGL shader compilation and hosted performance comparisons.
Keep their work separate from finite-face correctness. In particular, a lower
rim-roughness score alone cannot establish correctness because eroding coverage
also lowers it; retain independent geometry/coverage gates.

## Shared face-coordinate math

[Projected-face consolidation](../pr-screenshots/codex/projected-face-math/README.md)
shares the determinant and inverse coordinate solve across display sampling,
voxel face shadow baking and finite source queries. Quadrants, reflection and
winding are tested; ten native captures preserve their baseline RGB exactly.
Keep coverage ownership and authored versus revoxelized geometry explicit. SDF
ray intersection is not a projected quad and retains its separate implementation.

## SDF receiver prerequisite: opaque ownership

[Deterministic SDF winner publication](../pr-screenshots/codex/sdf-winner-ownership/README.md)
selects one submitted sample at the winning depth before color/identity writes.
This closes the opaque tie race prerequisite. Surface position/normal storage,
fragment receiver recovery, X-ray overlay ordering, and bounded shared geometric
caster queries remain pending. Measure and reduce the extra SDF election cost
without returning to independently raced surface fields.

### Sharp-shadow acceptance and optional softness

The target across every rendering mode is sharp, unblurred, geometrically
accurate projected shadows. Audit existing PCF/filtering and expose any retained
artistic softness as an explicit setting with a zero-softness path. Validate
receiver and caster mode combinations with softness disabled; filtering must
not hide parity, coverage, depth or projection errors. Zero softness alone does
not repair undersampled shadow geometry.

Before merging deterministic SDF ownership as a default, account for its measured
GPU cost (roughly 0.95–1.04 ms to 1.97–1.98 ms in the overlap fixture). Follow
the measurement-first next step in the cost experiment below before selecting
another optimization, or explicitly accept the tradeoff. Also investigate the
demo's `--no-lighting` path omitting SDF geometry so future geometry-only
captures can exercise the same rendering modes.

### SDF cost experiment and sharp-shadow sampling audit

Surface-query reuse was [tested and rejected](../pr-screenshots/codex/sdf-surface-reuse/README.md):
full-frame RGB stayed identical, but timings do not justify up to 64 MiB of
additional lane scratch. Production ownership is unchanged. Next split GPU
measurement into depth, owner election and publication without overlapping
timer scopes; compare warmed cardinal and noncardinal poses before optimizing.
The analytic-sphere shadow fixture retains surface speckling in both arms;
include it in receiver-geometry/self-shadow validation.

The current `ir_sun_shadow_sample` GLSL/Metal twins mix distinct contracts:
source-face ray queries and finite surface footprints use unfiltered coverage,
while the remaining map path in `sampleCascadeShadow` uses weighted 2×2 PCF.
`sunCascadeKernelInterior` derives near- versus far-cascade selection from that
2×2 kernel footprint, and `worldSunShadowFactorImpl` blends cascades. An explicit
zero-softness path must re-derive the selection gate and audit the blend as well
as the taps; removing PCF alone neither repairs finite coverage nor proves sharp
transitions across cascades. Preserve off-screen caster coverage and receiver-plane
depth handling while unifying the geometry queries.

### SDF dispatch attribution implemented

[Separate non-nested SDF timers](../pr-screenshots/codex/sdf-pass-timing/README.md)
now measure owner clear, depth, election, publication and casting. Fixed-pose
Metal captures preserve pixels with timing enabled/disabled. In two shadowed
sphere runs, casting is the largest row (3.024/2.744 ms), while individual
raster rows range 0.363–0.790 ms. Next isolate finite box casting, non-box
depth fallback, resolve and bake within that casting bundle before selecting
a change. Ownership cost remains a merge consideration; these timing scopes
are not an optimization and do not fix sphere speckling or sharp-shadow geometry.

### Finite analytic-box casting optimized

[Separated casting scopes and bounded sample partitioning](../pr-screenshots/codex/analytic-cast-dispatch/README.md)
identify and reduce underutilization on large analytic boxes. Metal box casting
in the sphere/floor fixture falls from 1.028 ms to 0.046/0.059 ms with exact RGB
parity; a small-box control also improves. This is a dispatch-only optimization,
not a geometry or filtering fix. Mixed large descriptor populations may not
benefit because the bounded fanout returns to one group per descriptor.

Pending: ownership cost still needs its own decision; Windows validation; general
SDF receiver geometry and sphere self-shadow artifacts; sharp sampling across
PCF/fallback/cascade paths.

### Box-only fallback eliminated

[Box-only submission gating](../pr-screenshots/codex/box-only-cast/README.md)
skips non-box clear/raster/resolve/bake for all-box canvas batches. Six full-frame
Metal comparisons are pixel-identical, including four camera quadrants; the
mixed sphere/floor control retains all fallback stages. The removed rows total
0.269 ms in the parent small-box profile. This is workload-specific evidence,
not an end-to-end performance claim. Windows execution and the visual work above
remain pending.

### Sphere artifact separated from shadow sampling

[Lighting isolation controls](../pr-screenshots/codex/sdf-lighting-normal-frame/README.md)
show only 24 of 6,016 sphere pixels changing when shadows are disabled; the
remaining pattern maps exactly to the three face-slot normal classes. Treat the
dominant artifact as surface/normal reconstruction work, with the small shadow
delta still unresolved. Main-canvas smooth-yaw directional normals also use a
different frame from shadow-query normals. A broad canvas-level rotation was
rejected because secondary canvases can use different producers. Preserve elected
surface provenance before unifying the two consumers; retain true lattice faces
where that is the selected representation. No production fix is claimed yet.

### Main-canvas normal frame corrected

[Explicit main-canvas lighting routing](../pr-screenshots/codex/main-canvas-normal-frame/README.md)
aligns continuous-yaw slot normals with the shadow lookup, while excluding
secondary, detached and per-axis routes. Native normals match the independent
inverse-yaw color expectation, cardinal images remain identical, and shader
mutation controls pass. This is a frame conversion only: synthetic SDF face
selection and exact surface metadata remain unresolved.

For retained surface metadata, keep election scratch independent of publication.
A candidate compact depth/normal payload must carry the source half-face anchor,
retain continuous hit depth before quantization, and define world/model frame and
density. Invalidate on every geometry reset (including no-SDF frames), unsupported
winners and later non-SDF overpainting. Assess memory and bandwidth before choosing
this over retained per-canvas descriptors; neither option is implemented yet.

### Canvas descriptor lifetime retained

Canvas-owned [descriptor uploads](sdf-receiver-geometry.md#canvas-owned-descriptor-uploads)
now preserve each producer's shape array and projection snapshot across other
canvas submissions. Buffers grow with the canvas's submitted descriptor count;
empty frames invalidate the active count while retaining capacity. This removes
the shared-upload lifetime obstacle without adding per-trixel geometry storage.
Next publish an elected descriptor reference and explicit validity, then retain
linear lighting terms and evaluate the finite receiver at the fragment position.
The current change does not fix SDF/GRID receiver edges or sphere face selection.

Proposed performance-validation follow-up: compare pinned base/head alternately
on the same CI host and record runner image, Mesa/LLVM renderer, CPU quota and
available threads. Recent historical EPYC baseline comparisons showed broad
voxel-only slowdowns despite a zero SDF stage, with clean retries. Keep failed
runs and existing thresholds; add an SDF-active workload to measure election cost.

- Implemented next SDF provenance slice: retain elected sample keys and padded tile lookup per shape canvas, with frame invalidation, owner-stride reset and capacity reuse. This preserves the SDF pass winner without another GPU pass. Final-winner validity after later writes, fragment bindings, linear lighting payload and exact finite receiver evaluation remain pending. Track aggregate per-canvas owner memory alongside GPU timing before widening the consumer.

- Implemented conservative SDF sample validity: completed non-X-ray submissions become eligible; geometry writes and clears invalidate the whole canvas, including equal-depth/equal-ID replacements. Per-texel invalidation, consumer bindings, linear lighting payload and finite fragment receiver evaluation remain pending. Raw custom GPU writers must use the geometry-write accessor or invalidate explicitly.


### Finite box compute receivers

- Implemented: retained selected-owner data now feeds finite main-canvas analytical
  box queries in the compute-shadow pass, with exact signed normals and continuous
  receiver positions. Both backends use the shared inverse-iso helper. Unsupported
  shapes, lattice rendering, private canvases and finite misses preserve fallback.
- Validated: executable geometry/selection/layout controls, native Metal box captures,
  and pixel-identical shadows-disabled ownership controls. See
  [evidence](../pr-screenshots/codex/sdf-box-shadow-receiver/README.md).
- Still pending: linear material/ambient lighting payload and finite queries at
  actual presentation fragments; analytical Lambert/sky normals; curved/rotated
  receivers; per-texel validity; native OpenGL smoke; GPU profiling of the added
  receiver reads. One-value-per-trixel shadow edges remain visibly jagged.


### Analytical box normal continuity

- Implemented exact finite-box normal forwarding from shadow compute to main-canvas
  Lambert, sky and normal debug through unused RGBA8 alpha. Specialized ordinary
  kernels exclude the carrier path. Shadows-disabled normals remain identical.
- Native captures and executed shader/mutation controls are recorded in
  [box lighting normals](../pr-screenshots/codex/sdf-box-lighting-normal/README.md).
- Remaining: preserve linear material/ambient/direct lighting inputs for finite
  per-fragment receiving; use actual fragment coordinates without extrapolating
  finite misses; curved/rotated geometry and native GL smoke. Current shadow
  outlines are still not accepted as the final sharp-edge result.


### Shared surface lighting math

- Consolidated ambient-preserving sun response and final display mapping across
  canvas/overflow compute and deferred source-face fragments, with executable
  two-backend composition/mutation controls and native output-equivalence captures.
- Still pending: retained linear SDF lighting payload and finite queries at actual
  presentation fragments. This consolidation does not itself change shadow edges.
- Implemented the sky hemisphere correction: shared `surfaceSkyLight` uses
  `max(-worldNormal.z, 0)` because world +Z points down. Executed six-axis and
  tilted-normal/yaw/AO controls cover both shader backends; native HDR probes
  cover analytical boxes and rotated source faces. Sun-shadow on/off controls
  are pixel-identical. See [sky evidence](../pr-screenshots/codex/sky-hemisphere-normal/README.md).
- Next: retain linear SDF inputs and query finite geometry at presentation
  fragments, including sky from the actual fragment normal. The sky sign fix
  does not resolve trixel-sized shadow outlines or add sky occlusion tracing.


### Selected finite receiver query

- Centralized elected-owner selection separately from continuous finite query
  coordinates, with executable fractional-query and fallback-preservation gates
  on both backends. Compute now consumes that shared contract.
- Still pending: linear SDF lighting retention, final-write validity through
  fog/post-lighting color operations, actual fragment receiver evaluation, and
  measured memory/bandwidth cost before broadening eligibility. No new payload
  allocation or visual edge improvement is claimed by the extraction.
