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

```sh
fleet-run IRCanvasStress --only orbit --focus-orbit 7 --focus-mixed-shape --no-spin --no-auto-rotate --pivot-origin --no-ao --no-shadows --subdivisions 1 --zoom 8 --debug-overlay unlit --auto-screenshot 10 --sweep-yaw 0 1.57079633 5
python3 scripts/render-mixed-canvas-metric.py <capture.png> --yaw 45
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

## Open: the SDF marker's own display

The strict footprint fails at every yaw. In a source-face canvas the texture
layer composites as raw rectangular texels, so at the capped density a unit
marker is a 2x3 texel rectangle rather than the hexagon its cell projects to
(missing top row, wrong corners: 420 missing and 1,463 wrong-owner pixels at
yaw 0). Under continuous yaw the sub-1 analytical SDF path writes a 2x3
diamond from every hit pixel and the marker dilates to about twice its area
(ratio 2.1 at yaw 45). Both are the SDF display in private canvases, the
campaign's SDF extent and ownership item, not the lifecycle; the strict numbers
are the target that item drives to zero. AO on a marker-free wireframe frame is
uniform, so the AO overlay is not a raster-survival control for this fixture.
