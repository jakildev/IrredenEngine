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
source-face representation. The detached projected-face path still needs that
representation corrected. CPU picking and general rectangular canvases keep their
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
revoxelized occupancy: derive the expected faces from that path's actual cells.

```sh
# Positive native control: one identity voxel, zoom 16, output scale 2.
python3 scripts/render-source-face-metric.py docs/pr-screenshots/codex/detached-display-artifacts/capture-1296.png --shape voxel --identity --yaw 0 --iso-scale 64 32 --normals
# Known failure which the lattice-consistency metric accepts.
python3 scripts/render-source-face-metric.py docs/pr-screenshots/codex/detached-display-artifacts/capture-1277.png --shape frame --yaw 45
python3 -m unittest discover -s scripts/tests -p test_render_source_face_metric.py -v
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

A geometry fix is complete when the applicable source-face gate passes and the
lattice checks remain consistent. Keep the known failing capture as a rejection
control. Do not bless it as a golden image, enlarge the boundary band with zoom,
blur the output, or classify a visible tooth as harmless solely because samples
or total face areas agree.

## Remaining renderer work

The default plain-detached producer still discards projected face boundaries at
cell-centroid sampling. Preserve source-face ownership and continuous projected
coverage through presentation, including multiple candidates where one storage
cell spans different faces. Reuse the engine's face-local projection mechanisms
where appropriate; do not allocate a screen-sized geometry buffer per entity or
add per-voxel CPU work. Prove the one-voxel/adjacent-face cases before scaling to
frame/octahedron, lighting, and performance. The shared parity helpers are a
maintenance improvement, not a fix for that representation loss.
