# Input-to-present latency: the serialized baseline

The `--auto-profile` report's `--- Input latency ---` section
([format](../../../scripts/perf/README.md#read-input-to-present-latency)) times
each rendered frame on the steady clock, from the input it used to its end.
This document is the reading of today's behaviour, Metal's blocking `present()`.
Every later change that overlaps CPU and GPU frames is judged against it.

- **Host:** Apple M4 Max, macOS 26.5.2, Metal, offscreen window mode paced at
  120 Hz, AC power (battery 99 %).
- **Tree:** master `345dbbf25` plus this change. The offscreen pacer fix that
  stops the pacer sleeping after a late frame (`227cadbec`) is in the tree. The
  pacer sleeps after a frame's end, so it never enters that frame's reading,
  but it delays the next frame's poll.
- **Build:** Release, stage profiling off (`profiling_enabled` and
  `gpu_stage_timing` false), 300-frame `--auto-profile` windows. Statistics
  cover the 225 steady frames of each run.
- **End event:** `gpu-complete` in every run: the GPU end of the frame's last
  command buffer, moved onto the steady clock through the host clock. The CPU
  saw each end at most 0.37 ms after it; the mapping error is at most 0.0015 ms.

## Scenes

| Scene | Command (through `repeat_profile.py --release --repeats 1`) | CPU / GPU ms per frame |
|---|---|---:|
| (a) light | `--target IRShapeDebug -- --auto-profile 300 --yaw 0 --zoom 1 --sdf-voxel-scene off --config-preset configs/perf/sdf-off.lua` | 1.91 / 4.36 |
| (b) million control, GPU-bound | `--target IRPerfGrid -- --auto-profile 300 --config-preset configs/perf/million-profiling-off.lua --yaw 0 --wave-freeze --no-overlay --pivot-origin` | 8.51 / 19.14 |
| (c) CPU-bound | `--target IRPerfGrid -- --auto-profile 300 --config-preset configs/perf/profiling-off.lua --yaw 0 --no-overlay --pivot-origin --worker-threads 0` | 9.61 / 4.01 |

CPU is the probe's `frame_start_to_submit_ms` mean and GPU its
`submit_to_present_ms` mean, each averaged over the three rounds. Scene (c) is
IRPerfGrid's own 64³ per-cell-wave scene with every system run inline on the
main thread (`--worker-threads 0`), so the CPU half is the longer one: 9.4 to
9.8 ms against 3.9 to 4.1 ms of GPU in each round.

## Baseline

Three interleaved rounds, a → b → c per round. Every run reported
`QUIET=guarded`, `RESULT=CLEAN`, 225 of 225 steady frames with an end, and one
command buffer per valid frame.

| Scene | Round | `input_to_present_ms` mean | p99 | `cursor_to_present_ms` mean | Frame mean ms | Updates / frame | Load (1 min) |
|---|---:|---:|---:|---:|---:|---:|---:|
| light | 1 | 10.982 | 17.148 | 6.362 | 8.44 | 0.5 | 12.09 |
| light | 2 | 9.884 | 15.701 | 5.329 | 8.34 | 0.5 | 8.06 |
| light | 3 | 10.167 | 15.448 | 5.843 | 8.33 | 0.5 | 5.99 |
| million | 1 | 26.275 | 27.194 | 24.268 | 27.84 | 1.7 | 9.92 |
| million | 2 | 26.297 | 26.940 | 24.334 | 27.83 | 1.7 | 6.77 |
| million | 3 | 26.179 | 26.833 | 24.197 | 27.68 | 1.7 | 5.10 |
| CPU-bound | 1 | 15.770 | 22.580 | 7.306 | 14.21 | 0.9 | 8.71 |
| CPU-bound | 2 | 15.814 | 23.242 | 7.164 | 14.15 | 0.9 | 6.35 |
| CPU-bound | 3 | 15.674 | 24.699 | 7.161 | 13.84 | 0.9 | 5.15 |

## Noise bound

The between-run spread is the largest round minus the smallest. A later
change's delta on a row has to clear that row's spread before it is a finding.

| Scene | `input_to_present_ms` mean (spread) | p99 (spread) | `cursor_to_present_ms` mean (spread) | Frame mean ms (spread) |
|---|---:|---:|---:|---:|
| light | 10.344 (1.098) | 16.099 (1.700) | 5.845 (1.033) | 8.370 (0.110) |
| million | 26.250 (0.118) | 26.989 (0.361) | 24.266 (0.137) | 27.783 (0.160) |
| CPU-bound | 15.753 (0.140) | 23.507 (2.119) | 7.210 (0.145) | 14.067 (0.370) |

How far these go:

- **The host was busy, not idle.** Other fleet panes were building and
  running, with a one-minute load average of 5 to 12. Each run's own
  quiet guard held (`QUIET=guarded`), which is the verdict the benchmark lease
  gives. Round 1 ran at the highest load and is the light scene's high round.
- **The light scene's spread is the widest mean spread**, about 1.1 ms.
  Its frames run at 120 Hz against 60 Hz fixed updates, so every other frame
  has no poll of its own and keeps the previous frame's, reading one frame
  (8.3 ms) longer. The series alternates (`run-1.txt`, `--- Input to present`),
  and the mean carries that mixture.
- **The million and CPU-bound means are tight**, 0.12 and 0.14 ms. Their p99
  rows are not: the CPU-bound p99 spreads 2.1 ms. A p99 claim needs more than
  three rounds.
- **The pipeline is serialized.** Frame mean is close to CPU plus GPU:
  27.6 against 27.8 ms (million) and 13.6 against 14.1 ms (CPU-bound). Every run
  reports 0 frames in flight at frame start.
- **The light scene's CPU and GPU halves move between rounds by up to 1.2 and
  1.3 ms with a flat frame mean**: offscreen pacing at 120 Hz holds the frame,
  so cost moves between the halves and the pacer's sleep without moving the
  frame.

Reports, summaries and manifests: [`baseline/`](baseline/). The manifests'
`<worktree>` is the measuring pool worktree.

## Positive control

`configs/perf/input-latency-delay-20ms.lua` is `sdf-off.lua` plus
`input_latency_test_delay_ms = 20.0`, a sleep between each frame's last OS-event
poll and its RENDER. Light scene, Debug, three interleaved pairs, every run
`QUIET=guarded` with 225 of 225 samples:

| Pair | Without delay, mean ms | With delay, mean ms | Rise ms | Updates / frame without → with |
|---:|---:|---:|---:|---|
| 1 | 10.206 | 28.824 | 18.618 | 0.5 → 1.8 |
| 2 | 10.007 | 28.503 | 18.496 | 0.5 → 1.8 |
| 3 | 9.608 | 28.722 | 19.114 | 0.5 → 1.8 |

Every rise is within 2 ms of the injected 20 ms (mean rise 18.74 ms). The rise
is short of 20 ms because the delayed frames (about 29.7 ms) always run a fixed
update and so always poll: the one-frame stale-poll term in the arm without the
delay (half its frames) is gone. The cursor sample comes after the sleep, so
`cursor_to_present_ms` rises by only 1.3 to 2.0 ms, the extra update work.
Reports: [`positive-control/`](positive-control/).

## The probe adds no submission

`IRPerfGrid --auto-profile 120 --no-overlay`, and the million control at 120
frames, Release, on this change and on its parent `345dbbf25`:

| Fixture | Tree | Valid / attempted frames | Command buffers | Steady frame mean ms |
|---|---|---:|---:|---:|
| default | parent | 120 / 120 | 120 | 8.73 |
| default | this change | 120 / 120 | 120 | 8.55 |
| million | parent | 120 / 120 | 120 | 27.76 |
| million | this change | 120 / 120 | 120 | 27.76 |

One command buffer per valid frame in both trees. The probe reads clocks around
the existing commit and wait; it adds no GPU wait and no submission. Reports:
[`command-buffers/`](command-buffers/).

## A report with the fields

[`report-fields/irperfgrid-debug-120.txt`](report-fields/irperfgrid-debug-120.txt)
is `IRPerfGrid --auto-profile 120 --no-overlay`, Debug: 90 samples for its 90
steady frames, `End event: gpu-complete`, 0 frames in flight, and the
per-frame series after the update-tick series.

## Not measured here

- **OpenGL.** The OpenGL device reports `End event: not measured`. Its frame
  ends, and its reading of up to two frames in flight, come with the OpenGL
  side of the probe.
- **Presentation time.** A visible Metal window also reports `gpu-complete`.
  The drawable's presented time arrives later, on a Metal thread, and no
  unattended run has a drawable to exercise it.
