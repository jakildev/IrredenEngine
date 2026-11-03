# GRID exact-fit plate on Windows/OpenGL

Host: Windows 10, `windows-debug` (MinGW Makefiles, OpenGL), Debug builds in
`<pool-7 worktree>/build`, 1280x720 captures (1x).

| Side | Commit | Tree |
|---|---|---|
| head | `05c76ee6b` (merge of the GRID destination-capacity fix) | unmodified |
| parent | `ef578cf13` (its first parent) | plus the `--focus-exact-fit-plate` fixture hunks of `creations/demos/canvas_stress/main.cpp` only; no engine change, no probe |

The parent has no fixture flag, so the control adds only the plate allocation
and the flag registration. The metric used on both sides is the head's
`scripts/render-grid-cell-metric.py` (exported from `05c76ee6b`).

## Commands

```
fleet-build --target IRCanvasStress
python3 scripts/render-verify.py --target IRCanvasStress --no-build --timeout 400
fleet-run --timeout 400 IRCanvasStress --auto-screenshot 6 --only orbit --focus-orbit 6 \
  --focus-exact-fit-plate --no-spin --no-auto-rotate --pivot-origin --no-ao --no-shadows \
  --debug-overlay normals --zoom 8 --subdivisions 1 --sweep-yaw 0 4.71238898 4
fleet-run --timeout 400 IRCanvasStress --auto-screenshot 6 --only orbit --focus-orbit 6 \
  --focus-exact-fit-plate --no-spin --no-auto-rotate --pivot-origin --zoom 8 \
  --subdivisions 1 --sweep-yaw 0 4.71238898 4
python3 scripts/render-grid-cell-metric.py <frame> --fixture plate --yaw <0|90|180|270> \
  --zoom 8 --view <normals|beauty>
```

The default `--timeout 60` is too short for the first GL shader compile on this
host (the sun-shadow compute programs link for about a minute); use 400.

## Results

`head/`: eight frames, one metric JSON each, `summary.json`.
`parent/`: the four normals frames, JSON, `summary.json`.

| Row | head `missing_exposed_cells` / visible cells / `extra_pixels` / `wrong_face_pixels` | parent `missing_exposed_cells` |
|---|---|---|
| normals 0 / 90 / 180 / 270 | 0 / 145 / 0 / 0 on all four | 1 on all four |
| beauty 0 / 90 / 180 / 270 | 0 / 145 on all four (silhouette and occupancy only) | not captured |

`render-verify-head.log` (head) and `render-verify-parent.log` (parent) are the
full harness output minus engine log lines.

- Head: the eight `grid-cell` rows PASS; 6 default-pass shots PASS; the
  shadow, normal-facing and orbit-geometry structural rows PASS; the
  `cylinder_receiver*` and other rows without a `windows-debug` reference SKIP.
- Other FAILs: `compare:compare_yaw0` and `compare:compare_yaw_q`
  (max delta 240, PSNR 54.6 / 54.3) fail identically on the parent build, so
  they are not caused by the fix.
