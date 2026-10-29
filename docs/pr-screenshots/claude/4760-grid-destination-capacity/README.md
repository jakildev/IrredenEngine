# GRID destination capacity: native evidence

Parent is `origin/master` `469402034` plus only the fixture flag
(`--focus-exact-fit-plate`); head is this branch. Both are macOS Release
builds on Metal (Apple silicon, AC power). Every capture command, the resolved
executable, and every image hash are in [captures.json](captures.json).

## Fixture

`IRCanvasStress --only orbit --focus-orbit 6` centers orbit 6, a GRID set posed
at +45° about +Y. `--focus-exact-fit-plate` allocates it as an exact-fit
12×1×12 plate (144 authored slots, 145 exposed destination cells) instead of a
12³ box (1728 slots, 610 exposed of 1740 covered). Shared arguments:

```text
fleet-run --build-dir <build> IRCanvasStress --auto-screenshot 30 --no-auto-rotate
  --no-spin --only orbit --focus-orbit 6 --pivot-origin --subdivisions 1 --zoom 8
  --sweep-yaw 0 4.7123889804 4 [--focus-exact-fit-plate]
```

Views: normals (`--no-ao --no-shadows --debug-overlay normals`), beauty (default
lighting), shadow (`--no-ao --debug-overlay shadow`). [capture.py](capture.py)
reproduces one view; [grade.py](grade.py) grades a directory.

## Displayed cells

`scripts/render-grid-cell-metric.py` derives the expected destination cells from
the authored box and an explicit double-precision rotation matrix, views them
under the camera yaw, and compares every pixel outside a one-pixel band.
[metrics-head.json](metrics-head.json) holds every head row and command;
[metrics-parent.json](metrics-parent.json) holds the nine parent frames that differ
from head (the other fifteen are byte-identical and grade identically).

| Fixture, view | Yaw | Parent: missing exposed cells / missing px | Head: missing exposed cells / missing, extra, wrong-face px |
|---|---|---|---|
| plate, normals | 0 / 90 / 180 / 270 | 1 / 1,349 at each yaw | 0 / 0, 0, 0 at each yaw (145 of 145 visible) |
| plate, beauty | 0 / 90 / 180 / 270 | 1 / 1,349 at each yaw | 0 / 0, 0, n/a at each yaw |
| cube, normals | 0 / 90 / 180 / 270 | 0 / 0 (byte-identical to head) | 0 / 0, 0, 0 at each yaw (244 visible) |
| cube, beauty | 0 / 90 / 180 / 270 | 0 / 0 (byte-identical to head) | 0 / 0, 0, n/a at each yaw |

| Parent, plate normals 0° | Head, plate normals 0° |
|---|---|
| ![parent](parent/plate-normals-yaw0.png) | ![head](head/plate-normals-yaw0.png) |

All 12 cube captures and three of four plate shadow captures are byte-identical
between parent and head; only head copies are stored. `parent/` holds the nine
frames that differ.

Metric controls ([metric-controls.json](metric-controls.json)): grading a head
capture at the wrong yaw (0° as 90°, 90° as 270°) or as the wrong fixture fails
with 32 to 139 missing exposed cells or 201,314 extra pixels; the parent beauty
frame fails with one missing cell.

## Direct-sun overlay

The shadow overlay is graded against rays through the expected occupancy
(`--view shadow --terminator-tolerance 0`):

| Fixture | Yaw | False / missed px, parent | False / missed px, head | Occlusion exercised |
|---|---|---|---|---|
| cube | 0 | 0 / 0 | 0 / 0 | no (all lit) |
| cube | 90 | 2,432 / 13,024 | 2,432 / 13,024 | yes |
| cube | 180 | 0 / 13,872 | 0 / 13,872 | yes |
| cube | 270 | 0 / 1,456 | 0 / 1,456 | yes |
| plate | 0 | 0 / 0 | 0 / 0 | no |
| plate | 90 | 2,432 / 0 | 2,432 / 0 | no |
| plate | 180 | 0 / 848 | 0 / 848 | yes |
| plate | 270 | 2,082 / 0 | 2,272 / 0 | no |

The disagreement sits on thin strips at the staircase's tread/riser junctions
and is pixel-identical on parent and head for the unchanged solid, so it belongs
to the main-canvas sun sampling, not to capacity. At plate 270° the added cell
carries the same class of false shadow as its neighbours (+190 px). No row meets
"zero false or missed interior shadow samples".

## Capacity readings

`--grid-capacity-probe <file>` appends one line whenever a reading changes.
Over a 2000-frame spinning run (about 1000 update ticks, more than one
revolution) each fixture wrote exactly one line, at update tick 1
([profile/probe.json](profile/probe.json)):

| Fixture | Authored | Capacity | Growths | Failures | Pool resident / peak / live slots | Budget |
|---|---:|---:|---:|---:|---|---:|
| plate | 144 | 216 | 1 | 0 | 216 / 360 / 360 | 262,144 |
| cube | 1728 | 1728 | 0 | 0 | 1728 / 1728 / 1728 | 262,144 |

The plate's peak of 360 slots is the migration: the 144-slot span is freed only
after the 216-slot replacement is seeded. The freed 144 slots stay stranded
until an exact-size allocation reuses them (`pool_live` minus `pool_resident`).
The admission budget is the pool size, capped at 2,000,000 destination cells.

## Release profile

`scripts/perf/grid_capacity_controls.py` ran four capture-free rounds of
{parent, head} × {plate, cube}, forward and reverse, 2000 frames each with the
set spinning (750 steady update ticks per run, more than the 600-tick
revolution). [profile/summary.md](profile/summary.md):

| Case | Frame mean ms | Steady mean / p95 / p99 ms | `RebuildGridVoxels` mean (min–max) ms |
|---|---:|---:|---:|
| parent-plate | 8.383 | 8.332 / 8.973 / 9.388 | 0.0215 (0.0050–0.1020) |
| head-plate | 8.373 | 8.335 / 8.902 / 9.401 | 0.0205 (0.0050–0.1210) |
| parent-cube | 8.373 | 8.332 / 8.887 / 9.266 | 0.1020 (0.0250–0.3460) |
| head-cube | 8.373 | 8.340 / 8.945 / 9.403 | 0.0905 (0.0240–2.4280) |

Frames are paced at about 120 Hz offscreen, so frame time measures the pacing
wait, not the rebuild. The rebuild means differ by less than their run spread;
there is no speedup claim. The benchmark wrapper reported a quiet-window breach
from another fleet pane (`uncovered` lease), so these rows are not
quiet-certified. The profile report keeps a per-frame series only for whole
frames, so `RebuildGridVoxels` has mean, min and max but no p95 or p99.

## Not covered

- OpenGL: no capture on this macOS host.
- Interior occupancy, AO and light volume for solids that omit interior cells.
