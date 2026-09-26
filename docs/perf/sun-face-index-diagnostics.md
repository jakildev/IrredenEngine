# Native source-face index diagnostics

The oblique analytical-box workload reaches the **per-tile limit**, not the
global face-record limit. At camera yaw 90°, 63 boxes plus the floor request
64 records and leave every tile complete. One additional box requests 65
records and makes 132 near-cascade tiles and 56 far-cascade tiles incomplete.
The [captures](../pr-screenshots/codex/sun-face-index-diagnostics/README.md)
show fine shadow-edge teeth appearing at this boundary.

This is a diagnostic result, not a rendering fix. Geometry changes slightly
when a caster is added; the comparison is not a same-geometry intervention.
The source receiver rejects exact lookup when a tile count exceeds 64, so
fallback eligibility at the sampled world point is confirmed. The captures
strongly implicate that transition in this artifact but do not certify every
edge pixel or explain unrelated parity artifacts.

## Probe contract

`IRCanvasStress --sun-face-index-probe --auto-screenshot N` installs a settled
capture hook. It calls the prefab-owned `writeSourceFaceIndexProbe`, which
reads the existing counters after an explicit GPU memory barrier and finish.
No shader counters, rendering branches or normal-frame readbacks are added.
The roughly 8 MiB tile-table readback, allocation and file output happen only
on requested screenshot frames. **Do not enable this probe during profiling.**

The working directory receives `sun-face-index-<shot-index>.csv`; those names
are overwritten on subsequent captures. Preserve them before another run.
Each active file contains the requested record count, record and tile caps,
sun basis, and every tile's counter, completeness and sun-UV bounds. Counts
are candidate reservations, not geometric hits or receiver queries. After a
global record overflow, the counter can include an incomplete sentinel;
do not interpret its magnitude as an exact candidate total in that case.

Inactive finite coverage, including legacy-depth or disabled shadows, writes
`active=0` and reports `written=false`. This replaces any old file for the
same shot rather than leaving stale valid evidence. The analyzer rejects
inactive captures. An I/O failure also reports false.

`scripts/perf/sun_face_index_probe.py` accepts CSV or gzip-compressed CSV,
checks tile coverage/uniqueness and the closed-capacity boundary, and reports
per-cascade occupancy/incompleteness. `--world-point X Y Z` projects a world
point using the captured sun basis and reports its tile in each cascade.
It does not decide which cascade a screen fragment selected, reconstruct
receiver geometry or count exact face intersections. World-point queries use
CPU arithmetic on captured float values; a near-boundary query is not a
bit-exact shader oracle.

## Native evidence

Apple M4 Max, macOS 26.5.2, Metal Debug. All four camera quadrants were
captured for counts 1, 64 and 128; the 63-box boundary control uses yaw 90°.
All 13 active captures completed cleanly. Separate native legacy-depth and
shadows-disabled controls emitted inactive files that the analyzer rejected. Production shader sources are
unchanged. Four 128-box frames match the previous probe-disabled capture
set pixel-for-pixel in RGB. Raw compressed tables, summaries and the binary
hash live in [the evidence directory](sun-face-index-diagnostics/).

| Boxes | Requested face records | Maximum tile counter | Incomplete near tiles across yaws | Incomplete far tiles across yaws |
|---|---:|---:|---|---|
| 1 | 2 | 2 | 0, 0, 0, 0 | 0, 0, 0, 0 |
| 63 (yaw 90° only) | 64 | 64 | 0 | 0 |
| 64 | 65 | 65 | 132, 132, 120, 132 | 56, 56, 56, 56 |
| 128 | 129 | 129 | 144, 154, 144, 154 | 64, 71, 64, 71 |

Every table has 32,768 tiles (two 128×128 cascades). At world `(0,0,0)`,
both cascade tiles have the maximum counter shown above. The record pool
capacity is 65,536; none of these runs exhaust it. Camera-dependent tile
counts reflect the fitted cascade grids, not a claim that caster geometry
changes with camera yaw. Native Windows/OpenGL remains unmeasured.

The failed sandbox display launch produced no usable capture and is excluded.
These synchronized diagnostic runs provide no performance measurements.

## Reproduce

Run from a configured dedicated worktree. Change count to 1, 63 or 128 for
the other controls; use a single 90° shot for the recorded 63-box control.

```bash
fleet-build --target IRCanvasStress -j3
fleet-run IRCanvasStress --only shadowbox,floor --probe-analytic-box --analytic-box-count 64 --analytic-box-yaw 0.785398163 --analytic-box-step 0.03125 0 0 --analytic-box-yaw-step 0.001 --sun-direction 0 0 -1 --no-spin --no-auto-rotate --pivot-origin --zoom 3 --subdivisions 1 --auto-screenshot 6 --sweep-yaw 0 4.71238898 4 --sun-face-index-probe
python3 scripts/perf/sun_face_index_probe.py build/creations/demos/canvas_stress/sun-face-index-0.csv --world-point 0 0 0
python3 scripts/tests/test_sun_face_index_probe.py
```

## Next work

Use a same-geometry experiment to keep the 65-entry fixture on an exact
query path and compare boundary pixels with finite-face geometry. Then
measure a bounded exact-overflow strategy or finer spatial indexing. Increasing
the global record pool cannot fix this tile limit. Rejecting false AABB
candidates may help outer tiles, but genuine overlap still needs a solution.
Do not smooth away the symptom. Retain the independent projection/orientation
checks for other rendering modes; this result does not supersede them.
