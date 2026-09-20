# `IRPerfGrid --yaw` unit: the rows labelled 45° were taken at 0.785°

`IRPerfGrid --yaw` is documented as radians, and every recipe under
`docs/perf/` passes `0.785398163` for a 45° pose. The demo handed that value
to `IRRender::setCameraVisualYaw`, which takes degrees. The camera sat at
0.785°, a near-cardinal pose on the per-axis scatter path. `IRCanvasStress`,
`IRShapeDebug` and `ir_voxel_yaw` pass their radians to
`IRPrefab::Camera::setYaw` and were never affected.

The demo now calls `IRPrefab::Camera::setYaw` too and logs the pose it took:

```
Initial camera yaw: requested_rad=0.785398 yaw_deg=45.000 residual_deg=-45.0000
```

## Control

A cardinal yaw runs the gather path and reports no per-axis entries; any other
yaw runs the scatter path. `90` is a cardinal only if the flag is degrees,
`1.5707963` only if it is radians, and the cull counts identify the pose.

Apple M4 Max, Metal, `macos-debug`, on battery. `--mode voxel_set --grid-size
64 --wave-freeze --zoom 4 --no-overlay --auto-profile 200`, two runs per pose
through `scripts/perf/repeat_profile.py`. Reports and manifests:
[perf-grid-yaw-unit/](perf-grid-yaw-unit/).

Before (master `021d41f7f`, binary `b4e41fb40c6cf666`):

| `--yaw` | Frame mean ms (runs) | GPU frame ms | Visible | AxisEntries | Path |
|---|---:|---:|---:|---:|---|
| 0 | 11.89 (11.51–12.27) | 8.49 | 255,275 | 0 | gather |
| 1.5707963 | 18.21 (17.84–18.58) | 13.64 | 259,765 | 779,295 | per-axis scatter |
| 90 | 10.12 (10.09–10.16) | 7.03 | 152,750 | 0 | gather |
| 0.785398163 | 19.45 (19.36–19.54) | 14.77 | 259,752 | 779,256 | per-axis scatter |
| 45 | 20.16 (20.12–20.19) | 15.40 | 255,246 | 765,737 | per-axis scatter |

After (binary `24ad4999f9271ab8`, same shaders and runtime scripts):

| `--yaw` | Pose logged | Frame mean ms (runs) | GPU frame ms | Visible | AxisEntries | Path |
|---|---:|---:|---:|---:|---:|---|
| 0 | 0.000° | 11.83 (11.81–11.85) | 8.54 | 255,275 | 0 | gather |
| 1.5707963 | 90.000° | 10.15 (10.04–10.26) | 7.05 | 152,750 | 0 | gather |
| 90 | 116.620° | 13.11 (13.03–13.19) | 9.12 | 183,636 | 550,906 | per-axis scatter |
| 0.785398163 | 45.000° | 19.66 (19.45–19.88) | 15.08 | 255,246 | 765,737 | per-axis scatter |
| 45 | 58.310° | 14.90 (14.54–15.26) | 10.79 | 245,161 | 735,482 | per-axis scatter |

The pair swaps. After the fix `1.5707963` reproduces the old `90` row's counts
(152,750 visible, no axis entries) and `0.785398163` reproduces the old `45`
row's (255,246 / 765,737) to the digit.

Captures never depended on the flag: every `--auto-screenshot` shot sets its
own yaw through the radians API, so the committed `IRPerfGrid` render-verify
baseline (three passes, seven shots, byte-exact thresholds) passes 21 of 21
on the fixed binary, and a before/after still of `--yaw` cannot be taken
through the shot table. The logged pose and the cull counts are the evidence.

`scripts/perf/repeat_profile.py` now fails an `IRPerfGrid` run whose logged
pose is not its `--yaw` in radians (`yaw_pose_mismatch` in the manifest), so
a profile can no longer be recorded at a pose other than the one it names.

## What this means for the committed tables

Seventeen evidence sets under `docs/perf/` hold 70 IRPerfGrid arms with a
non-zero `--yaw`: `current-frame-overflow-sort`, `frozen-scatter-flicker`,
`gpu-cost-attribution`, `gpu-frame-accounting`,
`light-volume-candidate-pruning`, `live-overflow-lighting`,
`million-entity-capacity`, `native-cpu-sampling`, `overflow-demand-capacity`,
`overflow-face-dedup`, `per-axis-dispatch-density`,
`per-axis-single-face-writer`, `rotation-controls`,
`rotation-subdivision-audit`, `static-upload-saturation`,
`voxel-cull-work-units` and `voxel-update-spans`. Read each row labelled 45°
as the 0.785° pose, and `rotation-controls`' near-cardinal arm
(`0.017453293`) as 0.0175° rather than 1°. The rows are not rewritten: their
manifests record the command that ran, and within one document both arms of a
comparison sat at the same pose, so before/after differences stand.

What does not stand without a re-run is any statement about 45° itself. At 64³
the two poses happen to read close (20.16 against 19.45 ms on the same binary,
3.7% apart), but yaw is not a free parameter on the scatter path: 58.3° reads
14.90 ms and 116.6° reads 13.11 ms in the same session. A single rotated pose
does not characterise rotation cost; the `--yaw` matrix axis (#3130) and a
continuous sweep are the controls that do. The objective's rotation-parity
baseline (2.06×, 30.3 / 14.7 ms) and the million-entity rows in
`million-entity-capacity.md`, `overflow-demand-capacity.md` and
`gpu-cost-attribution.md` are re-measured at a true 45° by the million
controls, the campaign slice stacked on this one.

The same commands now mean what their documents say. Re-running an old recipe
reproduces the scene its table describes, not the pose its numbers came from.
