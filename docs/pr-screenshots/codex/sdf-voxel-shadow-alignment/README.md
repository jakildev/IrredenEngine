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
  --auto-screenshot 6`, shots 1 and 3. The non-cardinal overlay still contains
  albedo-colored overflow regions; this is not evidence of complete AO coverage.

`ao-measurements.json` counts native RGB pixels in fixed ROIs: convex
`(1015,570)-(1120,675)`, corner `(1200,690)-(1380,815)`. Foreground means
`G>128 && B==0`; occluded means additionally `R>0` (AO overlay is red-to-green).
Both lit convex controls contain 10850 foreground pixels and zero occluded
pixels. The corner contains 20992 foreground pixels, with 4160 occluded before,
4096 after, and zero when AO is disabled. Peak occlusion red decreases from 51
to 30; the change deliberately replaces binary contact contrast with geometric
weighting. These counts establish retained contact, not exact physical AO.
