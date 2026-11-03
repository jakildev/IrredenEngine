# Renderer resource cleanup

Native macOS Metal, IRCanvasStress. Candidate built from
`6a7f8d768` plus this PR's uncommitted resource cleanup; executable and PNG
hashes are in [captures.json](captures.json).

Reference: the timing-on/off candidate captures committed in `848a625fc`,
in [the timer-cleanup evidence](../render-profile-cleanup/README.md).
There are no changes under `engine/` or `creations/demos/canvas_stress/`
between that commit and this PR's parent `6a7f8d768`.

All four PNGs are byte-identical to the corresponding references; decoded RGB
comparison also found zero changed pixels. Both native runs exited CLEAN.
The timing-enabled run reported 135 attempted/valid frames, zero invalid frames,
and 137 command buffers, including the screenshot submissions.

| Yaw | Timing on | Timing off |
|---|---|---|
| 0° | ![](on-cardinal.png) | ![](off-cardinal.png) |
| 45° | ![](on-rotated.png) | ![](off-rotated.png) |

After `fleet-build --target IRCanvasStress`:

```sh
fleet-run IRCanvasStress --auto-screenshot 10 --no-spin --no-auto-rotate --pivot-origin --zoom 1 --subdivisions 1 --sweep-yaw 0 0.7853981633974483 2 --auto-profile
```

Omit `--auto-profile` for the timing-off control. These are readback and unchanged
pixel checks, not performance measurements or a claim that the inherited rough
face patterns are fixed. Native OpenGL has not been run for this slice.
