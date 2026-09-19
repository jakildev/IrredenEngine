# Trixel face reconstruction: consistency versus geometry

A trixel is a sample and an ownership decision, not a rectangular screen pixel.
The displayed half-face must agree with the producer's face basis, local origin,
parity, subdivision and projection. Two halves of a planar voxel face must join
into that face's projected quadrilateral. Moving the canvas cannot change this
geometry. Staircases explicitly present in resampled occupancy remain geometry;
extra teeth introduced by the display lattice do not become correct by agreeing
with a sample-center oracle.

## Two independent contracts

| Contract | Evidence | What a pass cannot establish |
|---|---|---|
| Storage/display consistency | Cell centroids, triangle-interior probes, origin-parity reversal | That the displayed triangle union matches the source faces |
| Source geometry fidelity | Projected source polygons at framebuffer pixel centers, face-specific normal regions | Lighting, temporal stability, depth or arbitrary multi-voxel internal occlusion |

The frame in capture 1277 passes the first contract and fails the second.
A single selected centroid expanded into a complete fixed-lattice triangle loses
where the rotated source face actually crossed the triangle. Along the silhouette
and between face normals, this produces notches, spikes and wrong face ownership.
Increasing density shrinks the defects without repairing that information loss.
Flipping one global parity bit cannot generally reconstruct the missing boundary.

## Coordinate ownership and standardization

| Quantity | Owner / shared operation | Must not absorb |
|---|---|---|
| World-to-view rotation | Complete model-to-view transform used by producer | Independently snapped orientation for just one stage |
| Private storage origin parity | `localTrixelOriginParity` in the backend's `ir_iso_common` | Final quad world placement or camera pan |
| Stored local cell center | `localTrixelCellCentroid` | A second per-call-site triangle orientation formula |
| Fragment-to-local-cell mapping | `localTrixelFramebufferSamplePosition` | A second row correction in the caller |
| General canvas / picking mapping | `trixelFramebufferSamplePosition`, `trixelOriginModifier` | Private-display assumptions applied to all canvas types |
| Backend framebuffer origin | Existing GL/Metal adapter | A second parity flip intended to compensate for Y inversion |

GLSL and Metal mirror these contracts. Sharing arithmetic prevents divergence;
it does not establish that fixed local triangles are sufficient for a projected
source-face representation. Plain detached presentation retains source-face records
and projects their quadrilaterals; its raster remains an occluder approximation
for screen-space AO. CPU picking and general rectangular canvases keep their
existing contracts; this refactor does not claim picking parity has been proved.

## Deterministic gates

`render-detached-face-metric.py` reports `scope: lattice_consistency`.
Its deliberately flipped-parity control remains useful, but its `pass` is scoped.
Use `render-source-face-metric.py` for framebuffer source fidelity. It projects
authored voxel faces independently, with no trixel selector, no image registration,
no screenshot reference and no zoom-scaled error allowance. It exits 1 for any
missing/excess pixel outside a fixed one-framebuffer-pixel Chebyshev boundary
band. This band accommodates raster edge inclusion; it does not filter the image.
An empty capture fails. A clipped expectation fails. A fixture whose boundary
band consumes all testable interior (or a face interior in normal mode) fails
as insufficient resolution. Error maps show missing
pixels in cyan, excess in red, and wrong normal-face interiors in magenta.

For one voxel, `--normals` checks which projected face owns each interior pixel,
not merely whether a color appears in a valid palette. For a frame or octahedron,
the gate checks the silhouette union only; it does not infer the frontmost face
of intersecting projected cells from draw order. Known fixture shape, pose,
resolution and scale are required. Do not apply the authored-source oracle to
revoxelized occupancy: `render-revox-face-metric.py` derives the expected faces
from that path's actual cells (the anchored inverse resample of the fixture's
authored solid under the camera-composed rotation) and checks silhouette and
per-pixel owner face the same way. Its contract, controls and native evidence:
[revoxelized display fidelity](revoxelized-display-fidelity.md).

```sh
# Positive native control: one identity voxel, zoom 16, output scale 2.
python3 scripts/render-source-face-metric.py docs/pr-screenshots/codex/detached-display-artifacts/capture-1296.png --shape voxel --identity --yaw 0 --iso-scale 64 32 --normals
# Known failure which the lattice-consistency metric accepts.
python3 scripts/render-source-face-metric.py docs/pr-screenshots/codex/detached-display-artifacts/capture-1277.png --shape frame --yaw 45
python3 -m unittest discover -s scripts/tests -p test_render_source_face_metric.py -v
# Revoxelized cube: the display must match its own resampled cells, not the authored cube.
python3 scripts/render-revox-face-metric.py docs/pr-screenshots/claude/million-entity-render-face-parity/revox1_cyan_normals_yaw45.png --fixture cube --yaw 45
```

The hermetic suite is discovered by `render-harness-tests.yml`: independent
literal cube faces pass at two scales; added spikes, missing interiors, blank
frames and equal-area face-color swaps fail. This gates the validator itself in
CI. Native screenshots remain author/reviewer-time gates, not an automated GPU
render job. Passing harness tests does not make a known failing capture acceptable.

## Render acceptance matrix

Start with one rotated voxel and two adjacent coplanar voxels, then a thin frame.
Check both silhouette and the internal face diagonal at native pixels. Exercise
identity and intermediate rotations, both canvas-origin parities, negative and
fractional translations, camera pan, odd/even effective subdivisions, and world
placement versus screen locking. Add attachment transforms and backend execution
when those paths change. Report untested cases; do not turn a nine-angle sweep
into a claim about the entire matrix.

A geometry fix requires the applicable source-face gate to pass. Lattice checks
apply to paths which reconstruct local triangles; they are not an acceptance
target for continuous source-face presentation. Keep the known failing capture as a rejection
control. Do not bless it as a golden image, enlarge the boundary band with zoom,
blur the output, or classify a visible tooth as harmless solely because samples
or total face areas agree.

## Source-face presentation

Plain `DETACHED` canvases publish `SOURCE_FACES` for normal presentation. Stage 2
emits at most three exposed face records per compacted source voxel, independent
of zoom or subdivisions. Each record carries its source center, face identity,
albedo and owner. The records remain owned by the canvas until composition;
shared voxel upload buffers may be overwritten by other canvases in between.

The existing GPU overflow sorter orders a compact index list by source voxel and
face. The draw follows this list so equal-depth fragments have deterministic
ownership without depth bias or a CPU readback. Hardware triangles join into each
continuous projected face; the rendered model-to-view snapshot supplies both
corners and interpolated surface depth. No blur, coverage dilation or expansion
of a selected centroid into an unrelated screen triangle is involved.

Directional lighting uses the record's source normal and albedo. Screen-space AO
uses the source face center and transformed tangents as its receiver, probing the
raster only for neighboring occluders. It does not inherit AO from whichever
face happened to win a center texel. This remains approximate, face-flat AO;
hidden/off-screen occluders are not represented. Plain detached world shadow
casting and receiving remain separate work.

Source voxels do not also paint the texture color layer. SDF/overlay content can
still occupy that layer, receive its own lighting dispatch, and composite with
the source faces in the same framebuffer depth domain. Raw rectangular trixel
presentation remains diagnostic-only. Revoxelized occupancy keeps its own local
triangle reconstruction and real staircase normals.

Private source-face memory is bounded by three records per live source slot plus
a power-of-two index-sort allocation. Reuse avoids frame-by-frame allocation;
there is no CPU voxel geometry generation. Sorting, per-canvas dispatch cost and
retaining the AO raster are performance costs to measure. This path does not
establish scalability for a million independently allocated private canvases.

## Remaining acceptance and follow-up

- Extend independent face ownership/depth checks beyond one convex voxel.
- Exercise fractional/negative translations, attachments, screen locking and
  continuous motion as separate placement/depth contracts.
- Validate AO contact behavior and the green revoxelized canary separately;
  source-face normal captures cannot certify those lighting paths.
- Mixed private SDF/voxel density and recentering are gated by the
  [lifecycle contract](mixed-private-canvas-lifecycle.md); the SDF marker's raw
  texel display and continuous-yaw dilation in a private canvas remain open.
- Execute OpenGL validation on a supported host.
- Profile bounded face sorting, raster/lighting dispatches and memory before
  broader rotation/subdivision optimization.
