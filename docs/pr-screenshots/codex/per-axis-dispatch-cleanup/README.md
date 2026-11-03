# Per-axis dispatch cleanup

Native macOS Metal, IRCanvasStress, timing disabled. Baseline
`7d4b5cbc43103aad1da4f6051ca17339c5a7f692` and candidate
`31d4382bd7b050b79961f4093dd60cbe7aa55665` both include the merged store-frame
and fog changes through master `bc9a3d5a5`.

Both candidate PNGs are byte-identical to their baseline. Both runs exited
CLEAN. The shader-only candidate uses the same executable as the baseline;
shaders are loaded from the checked-out source tree. Commands, source commits,
executable hashes and PNG hashes are recorded in [before.json](before.json)
and [after.json](after.json).

| Yaw | Before | After |
|---|---|---|
| 0° | ![](cardinal-before.png) | ![](cardinal-after.png) |
| 45° | ![](rotated-before.png) | ![](rotated-after.png) |

```sh
fleet-run IRCanvasStress --auto-screenshot 10 --no-spin --no-auto-rotate --pivot-origin --zoom 1 --subdivisions 1 --sweep-yaw 0 0.7853981633974483 2
```

These captures check unchanged rendering, including inherited rough face patterns;
they do not establish visual fixes or performance improvements. Native OpenGL
has not been run for this slice.
