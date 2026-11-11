# Metal offscreen pacing: a late frame no longer sleeps an interval

The Metal `offscreen` window mode paces each frame to the primary display's
refresh interval, standing in for the `nextDrawable` gate a presented frame
has. Until this change a frame that finished after its deadline reset the
deadline to its completion and then slept one full interval, so every
over-interval frame carried one extra refresh interval. Every unattended
`--auto-profile` run goes through this path, because `ir-run` launches it in
`offscreen`. A late frame now returns without sleeping and the schedule
re-anchors at its completion; an early frame still sleeps to one interval past
the previous deadline. The sleep is the `offscreenPacing` row of the report's
`--- CPU phase timing ---` section, one sample per frame (0 ms when the frame
did not sleep), written in every build type.

OpenGL is unaffected: its offscreen mode is unthrottled (`swapInterval 0`).

## Conditions

Apple M4 Max, Metal, built-in Liquid Retina XDR panel, AC power, fleet live
(1-minute load 4.7 to 6.1). Release `build-release` of `d5f23eb3f` plus this
change, binary `c7fc50163739314a`. Every run went through
`scripts/perf/repeat_profile.py --release --repeats 3` with `RESULT=CLEAN` and
`QUIET=guarded`. A Debug run of the light scene before and after the arms
logged `Discovered 1 display monitors` and `paced at 120 Hz`
([rate-check.txt](offscreen-pacing-late-frame/rate-check.txt)): the interval is
8.33 ms. Reports, summaries and manifests:
[offscreen-pacing-late-frame/](offscreen-pacing-late-frame/).

The "before" column is the measurement recorded on issue #4947 (same host,
Release `480f39294`, the same commands, 2026-11-10 and 2026-11-11).

Remainder = frame mean − (sum of the per-system `Total(ms)` column / frames) −
GPU `envelope` mean.

## Million yaw-0 control

`--wave-freeze --no-overlay --pivot-origin --auto-profile 300 --config-preset
configs/perf/million-profiling-off.lua --yaw 0`

| | Frame mean ms | Remainder ms | Fixed updates / frame | `offscreenPacing` mean ms (samples) |
|---|---:|---:|---:|---:|
| Before (6 runs) | 46.61 (one run) | 11.79–11.99 | 3.9 | no row |
| After run 1 | 32.97 | 1.32 | 1.9 | 0.000 (300) |
| After run 2 | 32.97 | 1.33 | 1.9 | 0.000 (300) |
| After run 3 | 33.52 | 1.38 | 2.0 | 0.000 (300) |

The frame mean falls by 13.6 ms, more than the 8.33 ms interval, because a
shorter frame accrues fewer fixed updates (3.9 → 2.0 per frame).

## 64-cubed cardinal cell, zoom 4

`--auto-profile 300 --grid-size 64 --zoom 4 --yaw 0`

| | Frame mean ms | Steady frames under 16.67 ms | Steady minimum ms | Frames 10–16 ms | `offscreenPacing` mean ms |
|---|---:|---:|---:|---:|---:|
| Before (3 runs) | 20–22 | 0 of 675 | 19.6 | — | no row |
| After run 1 | 10.10 | 225 of 225 | 8.34 | 173 | 0.454 |
| After run 2 | 10.21 | 225 of 225 | 8.42 | 172 | 0.428 |
| After run 3 | 10.12 | 225 of 225 | 8.41 | 153 | 0.457 |

Before, frame times avoided the band between one and two intervals (2 of
11,920 frames over 40 runs fell between 10 and 16 ms). After, a frame whose
work exceeds the interval reads its work time.

## Light scene: on-time pacing is kept

`--auto-profile 300 --grid-size 16 --zoom 1 --yaw 0 --wave-freeze --no-overlay
--pivot-origin`

| | Steady p50 ms | `offscreenPacing` mean ms (max) |
|---|---:|---:|
| Before (2 runs, battery power) | 8.33, 8.33 | no row |
| After run 1 | 8.34 | 2.034 (6.195) |
| After run 2 | 8.35 | 1.429 (4.079) |
| After run 3 | 8.33 | 2.510 (5.899) |

A scene whose work fits in the interval still runs at the display cadence,
which `--auto-record` relies on for its simulation rate.

## A sleeping display changes the interval

With the host's display asleep, GLFW reports no monitor and the pacer takes its
60 Hz fallback: a Debug run logged `Discovered 0 display monitors` and `paced
at 60 Hz`, and the same light scene read 16.7 ms per frame. Wrap a timing
window in `caffeinate -d -i -u` and check the logged rate before comparing
runs.

## What this means for the committed tables

Metal wall-frame times in `docs/perf/` measured from 2026-10-04 (the offscreen
mode and its pacer) until this change include one refresh interval on every
frame that took longer than one interval; frames under the interval are
unaffected, as are GPU envelope and per-stage GPU timings and per-system CPU
times. The evidence sets with a macOS manifest from that window are
`bounded-light-volume`, `current-master-qualification`,
`fog-ceiling-retention`, `grid-receiver-adoption`, `sdf-voxel-profile`,
`sun-overflow-production`, `sun-overflowed-tile-query`,
`surface-adoption-controls`, `surface-lighting-controls` and
`voxel-workgroup-occupancy` (taken on the day the pacer merged; its binary
decides). Records from that window without a `repeat_profile.py` manifest
carry the term on the same terms when their frame times are Metal offscreen
runs. The tables are not rewritten: their manifests record the command that
ran. Within one document both arms of a comparison carried the same pacer: a
difference between two arms that were both over the interval keeps its sign,
not necessarily its size (fixed updates per frame grow with frame time), and a
difference between an arm under the interval and one over it is overstated. The documents that describe
the late-frame sleep as intentional predate this change; read them against
it.
