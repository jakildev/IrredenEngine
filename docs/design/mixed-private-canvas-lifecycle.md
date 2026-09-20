# Mixed private canvas lifecycle

A private entity canvas can hold two producers: a voxel pool rastered by
`VOXEL_TO_TRIXEL_STAGE_1` and analytic shapes rastered by `SHAPES_TO_TRIXEL`
(`C_ShapeDescriptor::canvasEntity_` names the canvas). The shape pass used to
treat every non-main canvas as its own: it cleared the canvas, anchored the
raster on the first shape's world position, rastered at the global effective
subdivision and snapped the camera yaw to a cardinal. On a canvas the voxel
pass had already rastered, the voxel colour was wiped while its depth lingered
in the Metal image-atomic scratch, so the shapes lost the depth contest against
a solid that no longer showed; on a canvas whose density the composite divides
out, the shapes appeared scaled and displaced by the cap ratio and left the
framebuffer under yaw.

## Contract

An entity canvas rasters in its owner's model frame. The composite
(`ENTITY_CANVAS_TO_FRAMEBUFFER`) places the whole canvas at the owner's
continuously yawed iso position and scales it by camera zoom over the density
the voxel producer stored (`C_TriangleCanvasTextures::renderedSubdivisions_`).
A shape targeting that canvas therefore rasters:

- at its offset from the owner's translation, never its world position
  (`SHAPES_TO_TRIXEL` snapshots the owners once per frame from the
  `C_EntityCanvas` archetype);
- with no camera term (`cameraTrixelOffset` zero);
- at the canvas's rendered density, which is the density the composite divides
  out (`max(renderedSubdivisions_, 1)`);
- under the continuous camera yaw whenever the camera sits off a cardinal
  (the smooth-yaw SDF path), because the composite applies no rotation of its
  own;
- into the voxel raster as it stands: a canvas the voxel pass rastered this
  frame (`renderedSubdivisions_ > 0`) is never cleared by the shape pass, and
  its depth test runs against the voxel depths. A shape-only canvas is reset
  through `IRSystem::clearCanvasAndDistances`, the same sentinel and Metal
  scratch mirror the voxel pass uses.

A revoxelized canvas stores the pool's half-cell phase in
`C_TriangleCanvasTextures::renderedCellOffset_` (-0.5 per even-sized centered
axis at density 1, view-local): the voxel raster stores cells at rounded
lattice positions and the composite places the whole canvas at the owner
plus the phase, so every displayed cell sits at integer + phase. A shape
rastered into that canvas is a lattice occupant too: the shape pass drops
the phase from the shape's owner offset (rotated into the world frame the
offset is projected in), the shader's per-axis rounding lands the shape on
a lattice cell, and the composite's phase puts that cell where the voxels
are, in-plane and in depth. A shape centred on a lattice cell displays
exactly; one between cells displays at the nearest cell, the voxelization
quantum the voxels themselves have. Without the phase drop the raster
rounds the raw offset and, for a shape less than half a cell below a cell
centre, picks the wrong neighbour (a whole cell, two iso rows). Shifting the
raster by the phase's iso projection instead is rejected: it keeps the
centroid but moves the shape's texels by an odd row, which flips the parity
the local-triangle gather reads, and the shape's hexagons come out as bow
ties.

Off the cardinals the same holds: an entity canvas sets `latticeShapes` in
the shape frame data, and a density-1 shape under smooth camera yaw takes
the lattice walk with its SDF query rotated by the continuous yaw
(`snapLatticeWalkYawed`), anchored on the snapped view cell and emitting the
plain 2x3 block, instead of the analytical surface the smooth path samples
at every iso pixel of both parities (which painted a 2x3 diamond per hit and
dilated a unit marker to 1.67 times its area). The lattice stays the integer
view lattice the canvas's voxels occupy; only the query point turns. A
shape with an entity rotation of its own keeps the general rotated search.

`SHAPES_TO_TRIXEL` runs after `VOXEL_TO_TRIXEL_STAGE_1` in a pipeline that
mixes the two producers on one canvas; the ordering table in
`engine/prefabs/irreden/render/CLAUDE.md` carries the row. Canvases with no
`C_EntityCanvas` owner keep the previous first-shape anchoring.

## Gate

`scripts/render-mixed-canvas-metric.py` projects the focused orbit frame's
authored faces and the two unit SDF box markers at world (3,0,0) and (6,0,0)
independently, depth-tests them per pixel, and reads the `--debug-overlay
unlit` capture. Its default pass is the lifecycle: the frame is pixel-exact
outside a two-texel guard around each marker, and each marker is present with
its centroid within one texel of the expectation. `--control` compares a
capture against the same scene without markers outside the guards, which is
the raster-survival check on a revoxelized canvas whose colour lives in the
texture layer. `--strict` also requires the marker footprint itself to be
pixel-exact. Density-1 unit markers pass on the tested revoxelized canvases;
plain detached texture display and the symmetric 45-degree tie remain open
(below).

The lattice fixtures are the revoxelized proof solids with the same markers:
`--focus-revox 3` is a 12x12x11 box (phase (-0.5, -0.5, 0)) and
`--parity-extent 12 11 11` makes it the one-even-axis box (phase
(-0.5, 0, 0)). `--mixed-shape-at` places the markers (world entities) and
`--focus-offset` translates the owner; the metric's `--fixture`, `--markers`
and `--owner` mirror them, with the voxel expectation from the lattice
module the revoxelized-display oracle uses and each marker expected at its
nearest lattice cell.

```sh
fleet-run IRCanvasStress --only orbit --focus-orbit 7 --focus-mixed-shape --no-spin --no-auto-rotate --pivot-origin --no-ao --no-shadows --subdivisions 1 --zoom 8 --debug-overlay unlit --auto-screenshot 10 --sweep-yaw 0 1.57079633 5
python3 scripts/render-mixed-canvas-metric.py <capture.png> --yaw 45
fleet-run IRCanvasStress --only revox --focus-revox 3 --focus-mixed-shape --mixed-shape-at -8.5 -8.5 -8 --no-spin --no-auto-rotate --pivot-origin --no-ao --no-shadows --subdivisions 1 --zoom 8 --debug-overlay unlit --auto-screenshot 10 --sweep-yaw 0 1.57079633 5
python3 scripts/render-mixed-canvas-metric.py <parity-capture.png> --yaw 0 --fixture parity --markers -8.5 -8.5 -8 -5.5 -8.5 -8 --strict
fleet-run IRCanvasStress --only orbit --focus-orbit 2 --focus-mixed-shape --no-spin --no-auto-rotate --pivot-origin --no-ao --no-shadows --subdivisions 1 --zoom 8 --debug-overlay unlit --auto-screenshot 10 --sweep-yaw 0 0 1
python3 scripts/render-mixed-canvas-metric.py <sphere-with-markers.png> --yaw 0 --control <sphere-without-markers.png>
python3 scripts/tests/test_render_mixed_canvas_metric.py
```

## Evidence

Apple M4 Max, Metal, `macos-debug`, 2560x1440, zoom 8, private density 1,
unlit overlay. Retained under
`docs/pr-screenshots/claude/million-entity-render-mixed-canvas-lifecycle/`.

| Capture | Frame outside guard (missing / extra / wrong) | Marker centroid error px | Marker area ratio | Lifecycle |
|---|---|---|---|---|
| before, yaw 0 | 0 / 8,192 / 0 | (-615, -301) | 1.51 | fail |
| before, yaw 45 | 0 / 0 / 0 | markers absent | 0 | fail |
| after, yaw 0 | 0 / 0 / 0 | (-6.2, 6.1) | 0.92 | pass |
| after, yaw 22.5 | 0 / 0 / 0 | (-4.0, 7.0) | 1.76 | pass |
| after, yaw 45 | 0 / 0 / 0 | (11.5, 8.0) | 2.11 | pass |
| after, yaw 67.5 | 0 / 0 / 0 | (8.3, 10.8) | 1.55 | pass |
| after, yaw 90 | 0 / 0 / 0 | (4.8, 9.7) | 0.79 | pass |

Before the fix the second marker sat eight texels from its position at yaw 0
(the density-8 raster inside a density-1 canvas, anchored on the first
marker) and both markers left the framebuffer at yaw 45. After it every marker
centroid is within a third of a texel.

Raster survival on a revoxelized canvas (orbit sphere, index 2, with the same
markers, which sit inside the sphere and are legitimately hidden by it): with
the shape pass's unconditional clear restored as an experiment, the capture is
entirely black at yaw 0 and 45 (the sphere's colour cleared, the markers
losing the depth contest against its stale scratch depth), 235,823 and
248,327 pixels differing from the marker-free control; with the fix the
sphere-with-markers capture matches the control outside the guards
(`control_differing_pixels` 0, 276,480 and 280,576 sphere pixels present).
That control measures survival of the voxel raster only; how an SDF displays
inside a local-triangle canvas is the storage caveat on
`C_TriangleCanvasTextures`, not something this fixture claims.

### Lattice evidence

Same host and settings; captures under
`docs/pr-screenshots/claude/million-entity-render-shape-phase/`. The
expectation puts each marker at its nearest lattice cell; the frame column
is the voxel producer outside the marker guards (missing / extra / wrong
owner), pixel-exact in every capture, so the lattice module's resample is
the display's. Footprint is the whole frame including the markers; the
centroid error is in framebuffer pixels (one trixel is 32 x 16).

| Fixture, marker | Yaw | Before: footprint m/e/w, centroid | After: footprint m/e/w, centroid | Strict after |
|---|---:|---|---|---|
| parity box, on the lattice (-8.5, -8.5, -8) | 0 | 0/0/0, (0, 0) | 0/0/0, (0, 0) | pass |
| parity box, on the lattice | 90 | 0/0/0, (0, 0) | 0/0/0, (0, 0) | pass |
| parity box, quarter cell above a cell (-8.25) | 0 | 0/0/0, (0, 0) | 0/0/0, (0, 0) | pass |
| parity box, quarter cell below a cell (-8.75) | 0 | 0/0/7,680, (0, 32) | 0/0/0, (0, 0) | pass |
| parity box, owner (4, 2, -1) | 0 | 0/0/0, (0, 0) | 0/0/0, (0, 0) | pass |
| one-even-axis box, on the lattice (-8.5, -8, -8) | 0 | 0/0/0, (0, 0) | 0/0/0, (0, 0) | pass |
| parity box, on the lattice | 22.5 / 45 / 67.5 | (0, 16) / (16, 8) / (16, 16), area 1.67 | (0, 0) / (16, -8) / (16, 0), area 1.67 | fail (below) |
| orbit frame (zero phase) | 0 / 45 | (-6.2, 6.1) / (12.0, 8.0) | unchanged | fail (below) |

At the cardinals a shape on a revoxelized canvas is now the hexagon of its
lattice cell, pixel for pixel, whether it sits on a cell, a quarter cell
above one (which the raw rounding already got right, since round-half-up
carries -8.25 to -8 and the composite's -0.5 lands it on -8.5) or a quarter
cell below one (which the raw rounding sent a whole cell away, two iso rows
and 7,680 wrong-owner pixels). The translated owner behaves like the centred
one, which pins the owner-relative subtraction's sign. Off the cardinals the
shape pass rounds the iso projection instead of the cell and paints its
analytical 2x3 diamonds at both parities, the dilation below; that is the
next slice, not placement.

### Continuous-yaw evidence

Same host, settings and lattice-aware expectation; captures under
`docs/pr-screenshots/claude/million-entity-render-sdf-marker-display/`.
"Before" is the lattice snap alone (the raster still on the analytical
smooth path off the cardinals); "after" adds the yawed lattice walk.

| Fixture, marker | Yaw | Before: footprint m/e/w, centroid, area | After: footprint m/e/w, centroid, area | Strict after |
|---|---:|---|---|---|
| parity box, (-8.5, -8.5, -8) | 22.5 | 0/0/3,728, (0, 0), 1.67 | 0/0/0, (0, 0), 1.00 | pass |
| parity box, (-8.5, -8.5, -8) | 45 | 0/0/6,742, (16, -8), 1.67 | 466/1,472/1,845, (16, -8), 1.00 | tie (below) |
| parity box, (-8.5, -8.5, -8) | 67.5 | 0/5,275/1,410, (16, 0), 1.67 | 0/0/0, (0, 0), 1.00 | pass |
| parity box, (-8.5, -7.5, -8) | 22.5 / 45 / 67.5 | | 0/0/0, (0, 0), 1.00 at each | pass |
| parity box, owner (4, 2, -1), (-8.5, -8.5, -8) | 45 | | 0/0/0, (-0.5, -0.25), 1.00 | pass |
| one-even-axis box, (-8.5, -8, -8) | 45 | | 0/0/0, (0, 0), 1.00 | pass |
| orbit frame (source-face canvas) | 45 | 421/2,081/2,930, (12, 8), 1.67 | 875/671/1,703, (12, 8), 1.00 | fail (below) |

The tested unit markers on revoxelized canvases have the area of one lattice
cell, including off-cell positions, a translated owner and the one-even-axis
box. Their strict footprint passes except for the symmetric tie below. The symmetric marker (-8.5, -8.5, -8)
at exactly 45 degrees views to a y of exactly zero, half a cell from both
neighbours, and the shader's float32 rotation and the oracle's float64 one
round the tie apart. The asymmetric marker at the same yaw passes, which
isolates the numerical placement disagreement without resolving it. The orbit frame's marker is
now one cell (area 1.00 instead of 1.67), but a source-face canvas
composites the texture layer as raw rectangular texels, so its footprint
stays the open item below.

## Remaining display coverage

Plain `DETACHED` source-face canvases still composite the SDF texture layer as
raw rectangular texels. Their strict marker footprint fails; the voxel
source-face quads themselves are a separate producer. The yawed lattice walk
removes marker dilation but does not reconstruct the texture layer's hexagons.

The strict revoxelized evidence covers **unit, unrotated box markers at density
1**, including asymmetric positions, owner translation and mixed-parity extents.
It does not establish arbitrary rotated SDFs, higher density, camera pitch/roll,
or all shape kinds. A shape with its own rotation still bypasses the lattice
walk. The symmetric 45-degree case remains a numerical placement disagreement;
the passing asymmetric control isolates it but does not resolve the tie.

The yawed lattice walk must honor `SHAPE_FLAG_HOLLOW`: only samples with SDF
between -0.5 and 0.5 belong to the shell (the existing negative-bound epsilon
handles boundary noise). `--mixed-shape-size`, `--mixed-shape-hollow` and
`--mixed-shape-depth-color` extend the fixture beyond unit markers. A closed
five-cell box can have identical before/after pixels because nearer shell cells
cover rejected interior samples; that capture proves regression stability, not
that every hollow-shape case has been validated. The older cardinal lattice
walk's hollow behavior remains a separate audit item.
