# Trixel gather interpolation

The gather vertex and fragment stages carry canvas positions in **centered texel
units**, not normalized texture coordinates. For quad coordinate `p`, canvas size
`S` and texture offset `O`, interpolate `(p.x, -p.y) * S + O`, then add `S / 2`
in the fragment. The Metal clip-Y flip remains a separate backend adapter.

This is the same affine mapping as interpolating
`(p.x, -p.y) + 1/2 + O/S` and multiplying by `S` in the fragment, but the latter
introduces division/rescaling roundoff. At an exact integer sampling boundary,
that roundoff can change the selected raw texel and its triangle half. The result
is a missing edge, an extra edge, or a neighboring face's normal/color. Centered
coordinates keep the large canvas-center translation out of interpolation.

All gather variants share the coordinate contract, including analytical shapes.
Display and hover use the same reconstructed raw coordinate; parity correction
remains specific to the producer layout. This does not change texture storage,
filtering, the face lattice, buffer layouts, or the continuous scatter path.
Do not substitute a sampling epsilon or blur for a correct coordinate mapping.

## Deterministic coverage

`scripts/render-orbit-geometry-metric.py --yaw 0|90|180|270` checks the frozen
orbit-6 revoxelized cube against inverse-resampled integer occupancy. Cardinal
camera rays lie on an exact 1/96 world-coordinate lattice; integer traversal
selects the first occupied cell and signed world face without floating-point
tolerances. A simultaneous face crossing admits only the incident face normals.
Empty silhouette pixels are checked too. Negative controls corrupt individual
pixels into an extra silhouette, missing edge, and wrong normal and must fail.

The CanvasStress manifest runs these four zero-mismatch checks in addition to
the existing yaw135° oracle. `IRShapeDebug --gui-test` covers shared hover identity.
The [native capture evidence](../pr-screenshots/codex/scatter-boundary-ownership/README.md)
records the tested camera, canvas and output sizes. These fixtures are not proof
for arbitrary projection matrices or every GPU's interpolation precision;
native OpenGL presentation and other boundary poses remain separate coverage.
