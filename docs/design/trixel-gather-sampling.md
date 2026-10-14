# Trixel gather sampling

The gather fragment stage computes its raw canvas position from its own raster
pixel; no canvas coordinate is interpolated. For fragment pixel `p`
(`gl_FragCoord.xy`, Metal `position.xy`), quad centre `c` in raster-target
pixels, canvas texels per raster pixel `t`, canvas size `S` and texture offset
`O`, `canvasPositionFromPixel` (`ir_iso_common`) evaluates

    ((p.x - c.x) * t.x, -(p.y - c.y) * t.y) + O + S / 2

and `floor` of it selects the texel. `c` and `t` travel in
`FrameDataTrixelToFramebuffer::canvasFromPixel_`, written together with the
quad's model-projection by `IRRender::placeGatherQuad`; every gather producer
places through it. The vertex stage only positions the quad. Metal's clip-Y
negation makes its fragment `position.y` equal OpenGL's `gl_FragCoord.y` for the
same geometry, so the map has one sign convention on both backends.

Under the snapped zoom policy the centre sits on the half-pixel lattice and `t`
is a power of two, so every term is exactly representable and a pixel centre
that lands on a texel boundary selects the same texel on every GPU. An
interpolated varying gives no such guarantee: rasterizers evaluate it with
implementation-specific roundoff, and at an exact integer boundary a value a
hair below the integer selects the neighbouring texel and its triangle half.
The result is a missing edge, an extra edge, or a neighbouring face's normal or
colour along a whole row. Under continuous zoom the map is an ordinary affine
evaluation with ordinary roundoff; no exactness is claimed there.

All gather variants share the coordinate contract, including analytical shapes.
Display and hover use the same reconstructed raw coordinate; parity correction
remains specific to the producer layout. This does not change texture storage,
filtering, the face lattice, or the continuous scatter path. Do not substitute
a sampling epsilon, a bias, an interpolation qualifier or a blur for a correct
coordinate mapping.

## Debug-overlay colour codes

The NORMALS overlay encodes each unit-normal component through
`encodeUnitVectorUnorm8`, which rounds half up to an exact UNORM8 code before
the store: -1 → 0, 0 → 128, +1 → 255. A bare `n * 0.5 + 0.5` leaves the 0.5
midpoint to the hardware float-to-unorm conversion, which is
implementation-defined at that tie (the native-Windows NVIDIA OpenGL driver
stores 127 where Metal stores 128), so the overlay would otherwise be
backend-dependent for every consumer.

## Continuous surface queries

Texture addressing and a fragment's geometric ray use different origins. Raw
texture coordinates add `S / 2` to the centered pixel map; analytical shape
producers anchor their iso coordinates at `trixelOriginOffsetZ1(S)`. Before a
continuous fragment calls the finite shape receiver, `shapeFragmentQueryPixel`
converts raw coordinates with `raw - S / 2 + trixelOriginOffsetZ1(S)`.
Texture, owner and hover reads keep the raw coordinate. Compute receivers already
use producer storage coordinates and do not apply this conversion.

For even canvases the conversion subtracts one storage texel per axis. Odd
dimensions also require the half-texel difference between the floating center
and the integer producer origin; a fixed `(-1,-1)` offset is insufficient.
The connected `test_render_shape_receiver.py` control executes the production
fragment pixel map, raw-coordinate expression, conversion and box receiver
against an independent screen-ray intersection. Both missing conversion and
fixed-even-offset mutations must fail. Odd-size controls prove this query
algebra; they do not certify producer coverage or sampled depth for odd backing
sizes. The native fixture uses even backing sizes.

## Deterministic coverage

`scripts/render-orbit-geometry-metric.py --yaw 0|90|180|270` checks the frozen
orbit-6 revoxelized cube against inverse-resampled integer occupancy. Cardinal
camera rays lie on an exact 1/96 world-coordinate lattice; integer traversal
selects the first occupied cell and signed world face without floating-point
tolerances. A simultaneous face crossing admits only the incident face normals.
Empty silhouette pixels are checked too. Negative controls corrupt individual
pixels into an extra silhouette, missing edge, and wrong normal and must fail.
The fixture's gather samples one canvas column and two canvas rows per raster
pixel, so every pixel centre lands on an integer canvas row: the oracle is a
direct test of the pixel map's exactness on the host that renders it.

The CanvasStress manifest runs these four zero-mismatch checks in addition to
the existing yaw135° oracle on every backend. `IRShapeDebug --gui-test` covers
shared hover identity. `scripts/tests/test_render_gather_pixel_map.py` executes
both backends' `canvasPositionFromPixel` and `encodeUnitVectorUnorm8` source
through a C++ adapter. The [native capture evidence](../pr-screenshots/codex/scatter-boundary-ownership/README.md)
records the tested camera, canvas and output sizes. These fixtures are not
proof for arbitrary projection matrices or other boundary poses.
