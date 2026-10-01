# Finite face raster validation

An ideal ray and a hardware triangle rasterizer do not necessarily choose the
same face at every boundary sample. Hardware rounds projected vertices to a
subpixel lattice and assigns shared edges to one triangle. A mismatch against
an ideal ray is a diagnostic, not permission to ignore boundary pixels or to
declare the rasterizer wrong without checking its coverage rules.

The orbit fixture has three complementary checks:

- `render-orbit-geometry-metric.py`: independent integer camera rays at cardinal
  yaw, and an analytical section at yaw 135°. Keep these strict checks.
- `render-orbit-raster-metric.py`: project exposed finite faces of the same
  independently constructed occupancy, quantize their vertices, apply integer
  top-left coverage, and select the nearest covered face plane. It checks every
  output RGB pixel, including every pixel in each upscaled game-pixel block.
- An independent native Metal draw of exposed cube faces, without trixel
  encoding, storage, overflow, depth tie bands, or composition. The retained
  [source and evidence](../pr-screenshots/codex/scatter-centered-projection/README.md)
  ground the raster model separately from the engine output.

For an oriented edge from `a` to `b`, coverage uses the integer cross product
`E(p) = (b.x-a.x)*(p.y-a.y) - (b.y-a.y)*(p.x-a.x)`. After normalizing winding,
positive values are inside. A zero belongs only to the top/left member of the
shared edge. Vertices round to `floor(v*2^bits + 0.5)`; pixel centers are exact
integers on this lattice. Neither operation filters or blends face colors.

Eight fractional bits match the native Metal reference in the retained fixture.
This is an explicitly validated raster model, not a portable precision promise.
OpenGL needs a native reference before this model becomes its automated gate.
The command requires both the actual settled yaw from `CaptureCamera` and the
subpixel bit count; it accepts no mismatch tolerance. It remains an explicit
diagnostic alongside the backend-independent default render gates.

```bash
python3 scripts/render-orbit-raster-metric.py capture.png --yaw-radians 1.9634955 --subpixel-bits 8
python3 scripts/tests/test_render_orbit_raster_metric.py
```

The fixture is frozen orbit entity 6, inverse-resampled from a 12³ cube rotated
45° about Y, zoom 4, origin pivot, no AO or motion, normals overlay, and a
1280×720 game framebuffer. The output may have any positive uniform integer
scale. These checks do not certify other geometry, projection scales, lighting,
or arbitrary camera translation.

## Centered projection contract

Finite-face scatter subtracts the canvas center from its integer storage anchor
before adding projected world corners. Normalizing a large canvas coordinate
and then subtracting 0.5 loses fractional bits that decide edge ownership.
This is algebraically the same projection with a better-conditioned evaluation
order. It changes neither the face footprint nor the depth key and adds no
margin, snapping policy, blur, visibility rejection, or fragment work.

The shared gather has the related [centered texel sampling contract](trixel-gather-sampling.md).
Validate both against actual geometry: two paths agreeing is useful evidence,
but agreement alone does not establish that their common output is correct.
