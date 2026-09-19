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
axis at density 1, view-local), and the composite adds that phase to the
canvas placement and, scaled into its depth units, to the canvas depth, so
the voxel texels rastered at rounded cells land on their resampled centres. A
shape carries no phase, so the shape pass shifts its raster the other way:
the frame offset is the phase's iso projection negated
(`cameraTrixelOffset = -pos3DtoPos2DIso(phase)`), and the shaders subtract
`canvasPhaseDepth` (the phase's x+y+z, scaled to the subdivided depth units)
from every shape depth. Both are lattice moves: the frame offset is floored to
whole trixels and the depth bias rounds to the unit, so a phase whose iso
projection is fractional (exactly one of the x and y extents even) leaves a
shape up to half a trixel from the composite's placement, and an all-even
solid's 1.5-unit phase depth leaves it half a depth unit nearer. A phase
whose iso projection is whole (both or neither of x and y even) places the
shape exactly. Subtracting the rotated phase from the shape's 3D position
instead is rejected: the cardinal raster rounds each axis of a half-integer
position up, which moves the shape by a whole trixel per even axis.

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
pixel-exact; that measures the SDF marker's display in a private canvas and is
open (below).

The phase fixtures are the revoxelized proof solids with the same markers:
`--focus-revox 3` is a 12x12x11 box (phase (-0.5, -0.5, 0), iso projection
(0, 1), depth -1), `--parity-extent 12 11 11` makes it the one-even-axis box
(iso (0.5, 0.5), depth -0.5), and the cyan cube (`--focus-revox 1`, phase
depth -1.5) with a marker straddling its surface measures the depth bias.
`--mixed-shape-at` places the markers (world entities) and `--focus-offset`
translates the owner; the metric's `--fixture`, `--markers` and `--owner`
mirror them, with the voxel expectation from the lattice module the
revoxelized-display oracle uses.

```sh
fleet-run IRCanvasStress --only orbit --focus-orbit 7 --focus-mixed-shape --no-spin --no-auto-rotate --pivot-origin --no-ao --no-shadows --subdivisions 1 --zoom 8 --debug-overlay unlit --auto-screenshot 10 --sweep-yaw 0 1.57079633 5
python3 scripts/render-mixed-canvas-metric.py <capture.png> --yaw 45
fleet-run IRCanvasStress --only revox --focus-revox 3 --focus-mixed-shape --mixed-shape-at -8 -8 -8 --no-spin --no-auto-rotate --pivot-origin --no-ao --no-shadows --subdivisions 1 --zoom 8 --debug-overlay unlit --auto-screenshot 10 --sweep-yaw 0 1.57079633 5
python3 scripts/render-mixed-canvas-metric.py <parity-capture.png> --yaw 0 --fixture parity --markers -8 -8 -8 -5 -8 -8 --strict
fleet-run IRCanvasStress --only revox --focus-revox 1 --focus-mixed-shape --mixed-shape-at -5 -5 -5 --no-spin --no-auto-rotate --pivot-origin --no-ao --no-shadows --subdivisions 1 --zoom 8 --debug-overlay unlit --auto-screenshot 10 --sweep-yaw 0 0 1
python3 scripts/render-mixed-canvas-metric.py <cube-capture.png> --yaw 0 --fixture cube --markers -5 -5 -5 -2 -5 -5 --strict
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

### Phase evidence

Same host and settings; captures under
`docs/pr-screenshots/claude/million-entity-render-shape-phase/`. The frame
column is the voxel producer outside the marker guards (missing / extra /
wrong owner), which is pixel-exact for every revoxelized fixture in every
capture, so the lattice module's resample is the display's; the centroid
error is the markers' observed minus expected centroid in framebuffer pixels
(one trixel is 32 x 16).

| Fixture | Yaw | Before: centroid error px, marker px | After: centroid error px, marker px |
|---|---:|---|---|
| parity box | 0 | (0, 16), 6,144 of 6,144 | (0, 0), 6,144 of 6,144 |
| parity box | 22.5 | (-5.1, 16.5), 10,240 of 5,825 | (-5.1, 0.5), 10,240 of 5,825 |
| parity box | 45 | (-7.0, 11.0), 10,240 of 4,914 | (-7.0, -5.0), 10,240 of 4,914 |
| parity box | 67.5 | (5.4, 17.1), 10,240 of 5,855 | (5.4, 1.1), 10,240 of 5,855 |
| parity box | 90 | (0, 16), 6,144 of 6,144 | (0, 0), 6,144 of 6,144 |
| parity box, owner (4, 2, -1) | 45 | (3.2, 13.5), 10,240 of 4,914 | (2.8, -2.5), 10,240 of 4,914 |
| cyan cube, marker in its surface | 0 | (-50.2, -22.9), 6,144 of 2,816 | (-2.2, 1.1), 3,072 of 2,816 |
| one-even-axis box | 0 | (16, 8), 6,144 of 6,144 | (-16, -8), 6,144 of 6,144 |
| orbit frame (zero phase) | 0 / 45 | (-6.2, 6.1) / (11.5, 8.0) | unchanged |

At the cardinals the parity box's markers land exactly and the one-texel
error is gone; off the cardinals the remaining centroid error is the
continuous-yaw dilation below, the same at every yaw once the 16-pixel phase
term is removed. The translated owner behaves like the centred one, which
pins the owner-relative subtraction's sign. The cube's marker, half inside
the cube, showed in full before (the 1.5-unit phase depth put it in front of
the cube's cells) and shows 3,072 pixels against 2,816 expected after: the
integer depth lattice keeps it half a unit nearer. The one-even-axis box
keeps a half-trixel error with the sign flipped, the floored frame offset.
The zero-phase orbit frame is byte-for-byte the D0.1 result.

## Open: the SDF marker's own display

The strict footprint fails at every yaw. In a source-face canvas the texture
layer composites as raw rectangular texels, so at the capped density a unit
marker is a 2x3 texel rectangle rather than the hexagon its cell projects to
(missing top row, wrong corners: 420 missing and 1,463 wrong-owner pixels at
yaw 0). Under continuous yaw the sub-1 analytical SDF path writes a 2x3
diamond from every hit pixel and the marker dilates to about twice its area
(ratio 2.1 at yaw 45). Both are the SDF display in private canvases, the
campaign's SDF extent and ownership item, not the lifecycle; the strict numbers
are the target that item drives to zero; on the parity box at yaw 0 the whole
residual is that display (1,696 wrong-owner pixels: the marker draws as two
raw texel triangles, a bow tie, where its cell projects to a hexagon). AO on a
marker-free wireframe frame is uniform, so the AO overlay is not a
raster-survival control for this fixture.
