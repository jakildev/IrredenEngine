# Partial-face and shadow diagnostics

These are baseline captures, not a visual fix. Metal, 2560×1440, IRCanvasStress,
master 87642626c rendering code. Use native framebuffer coordinates for probes.

Shared arguments: `--only revox --focus-revox 2 --no-spin --no-auto-rotate
--pivot-origin --zoom 8 --no-ao --auto-screenshot 10`.

| Capture | Additional arguments | Oracle |
|---|---|---|
| grounded-unshadowed-yaw0 | `--no-shadows --sweep-yaw 0 0 1` | Triangles remain without shadows or AO |
| grounded-normals-yaw135 | `--debug-overlay normals --sweep-yaw 2.35619449 2.35619449 1` | Face ownership: 0 missing, extra or wrong-face pixels |
| grounded-shadow-yaw135 | `--debug-overlay shadow --sweep-yaw 0.785398163 3.926990817 5` (third shot) | 392 false-shadow and 392 missed-shadow interior pixels at zero terminator tolerance |

The errors image is the metric's diagnostic output: false shadows red, missed
shadows cyan. It is not a native render. Reproduce with:

```sh
python3 scripts/render-revox-face-metric.py docs/pr-screenshots/codex/shadow-quadrant-continuity/grounded-shadow-yaw135.png --fixture grounded --yaw 135 --shadow-overlay --terminator-tolerance 0 --explain-pixel 1508 672 --explain-pixel 1412 688
```

This command deliberately exits 1: the shadow defects are unresolved.
The expected visible cells are `(-5,2,-3)` and `(-5,-1,-4)`, respectively.
The first triangle has no expected sun blocker; the second is blocked by
`(-6,0,-4)`. The fixture's inferred destination occupancy is not a GPU readback;
resampling ties remain a possible source of disagreement on hidden cells.
