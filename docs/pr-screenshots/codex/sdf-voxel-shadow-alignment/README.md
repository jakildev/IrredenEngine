# AO contact evidence

Captured on macOS / Apple M4 Max / Metal, 2560x1440, from base `6c4f986e7`.
Before uses the base AO kernels; the new contact fixture is present in both
fixture runs. After uses this change. Crops are unscaled; complete frames are
included. SDF size experiments were reverted before these captures.

- `cube-*`: `fleet-run IRShapeDebug --auto-screenshot 6`, shot 3, zoom 4/yaw 0.
  Crop `(1100,450)-(1650,900)`. The SDF box size mismatch and sphere face pattern
  remain open; these images certify the AO band change only.
- `corner-*`: same command plus `--ao-contact-probe --debug-overlay ao`, shot 3.
  Crop `(980,520)-(1440,850)`. Disabled control adds `--no-ao`.
- `grid-*`: `fleet-run IRCanvasStress --no-auto-rotate --no-spin --frozen-pose 0.47
  --only gridspin,floor --zoom 4 --sweep-yaw 0 0.785398163 3 --debug-overlay ao
  --auto-screenshot 6`, shots 1 and 3; `grid-lit.png` is shot 1 of the same
  command without the overlay. The non-cardinal overlay still contains
  albedo-colored overflow regions; this is not evidence of complete AO coverage.
- `revox-yaw45-*`: same command with `--only revox,floor` and no `--frozen-pose`,
  shot 3 (yaw 45°), overlay and lit.

`ao-measurements.json` counts native RGB pixels in fixed ROIs: convex
`(1015,570)-(1120,675)`, corner `(1200,690)-(1380,815)`. Foreground means
`G>128 && B==0`; occluded means additionally `R>0` (AO overlay is red-to-green).
Both lit convex controls contain 10850 foreground pixels and zero occluded
pixels. The corner contains 20992 foreground pixels, with 4160 occluded before,
4096 after, and zero when AO is disabled. Peak occlusion red decreases from 51
to 30; the change deliberately replaces binary contact contrast with geometric
weighting. These counts establish retained contact, not exact physical AO.

## Rotated staircase (`scripts/render-ao-staircase-metric.py`)

The frozen-pose GRID cubes (0.47 rad about Z, X, Y and (1,1,1)) and the
revoxelized solids are real voxel staircases; every tread/riser corner is
locally a concave crease. `render-ao-staircase-metric.py <overlay> --lit
<lit>` masks the coloured solids from the lit frame and counts overlay pixels
the AO pass darkens (`occluded_frac`) plus the darkest one (`max_darkening`).
"Before" is the base kernels, "unguarded" the geometric weighting without the
tilt-aware resample (the first revision of this PR), "after" this change.

| capture | kernel | solid px | occluded | frac | max dark |
|---|---|---|---|---|---|
| GRID yaw 0 (`grid-ao-*`) | before | 379672 | 61704 | 0.163 | 0.302 |
| GRID yaw 0 | unguarded | 379672 | 105720 | 0.279 | 0.059 |
| GRID yaw 0 | after | 379672 | 46552 | 0.123 | 0.059 |
| GRID yaw 0 `--dominant b` (Y-axis cube) | before / unguarded / after | 90624 | 14896 / 27168 / 13984 | 0.164 / 0.300 / 0.154 | 0.20 / 0.059 / 0.059 |
| GRID yaw 0 `--dominant g` (X-axis cube) | before / unguarded / after | 88512 | 13376 / 28192 / 12640 | 0.151 / 0.319 / 0.143 | 0.30 / 0.059 / 0.059 |
| revox yaw 0 | before / unguarded / after | 276352 | 70400 / 101248 / 26624 | 0.255 / 0.366 / 0.096 | 0.20 / 0.059 / 0.059 |
| revox yaw 22.5° | before / unguarded / after | 231552 | 65792 / 67584 / 31488 | 0.284 / 0.292 / 0.136 | 0.20 / 0.090 / 0.090 |
| revox yaw 45° (`revox-yaw45-*`) | before / unguarded / after | 201600 | 104736 / 95232 / 52656 | 0.520 / 0.472 / 0.261 | 0.20 / 0.090 / 0.059 |

Without the resample the geometric weighting darkens roughly twice the
staircase area the base kernels did (the tread/riser corner is concave, so the
facing terms alone cannot reject it); with it restored, coverage is below the
base on every capture and the darkest step is 6% instead of 20-30%. The
inside-corner fixture is byte-identical with and without the resample
(`corner-after.png`, 4096 occluded): a genuine crease meets a multi-cell wall
and never returns to the receiver's face. The residual on the GRID cubes is the
same every-other-step pattern the base kernels leave.

GRID cubes at the non-cardinal camera yaws (22.5°, 45°) read zero occluded
pixels on all three kernels: they raster per-axis there, and a per-axis canvas
holds a single face, so no neighbour ever passes the different-face test. The
rotated-staircase check at non-cardinal yaw is therefore the revoxelized
solids (private single canvas), not the GRID cubes.
