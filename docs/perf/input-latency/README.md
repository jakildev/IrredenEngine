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

## OpenGL: frames in flight

OpenGL lets the CPU run ahead of the GPU. `present()` fences each frame with a
timestamp query, then waits until at most `max_frames_in_flight` presented
frames are unfinished. The default is 1. The OpenGL device reports each frame's
end as `gpu-complete`: the GPU timestamp of the frame's own fence query,
moved onto the steady clock through `glGetInteger64v(GL_TIMESTAMP)` read
between two steady-clock reads. The probe adds no query and no wait; a frame's
end is read when its fence retires.

- **Host:** Windows 10 19045, 32 logical CPUs, AC power, the NVIDIA OpenGL
  host of [`sun-overflowed-tile-query/`](../sun-overflowed-tile-query/README.md).
  Offscreen window mode, which flushes instead of swapping and is not paced.
- **Tree:** `ee09a9aef`, the code of this change before its documentation.
- **Build:** Release, stage profiling off, 300-frame `--auto-profile` windows,
  statistics over the 225 steady frames of each run.
- **Arms:** one binary. The bound comes from a config preset beside each
  scene's own: `configs/perf/million-frames-in-flight-<N>.lua` (scene b) and
  `configs/perf/profiling-off-frames-in-flight-<N>.lua` (scene c), with N 0
  (serialized), 1 and 2. Bound 2 is the queue before this change.
- **Rounds:** five. Each round runs scene b's three arms and then scene c's, with
  the arm order rotated each round. Three runs were contaminated when another
  pane's lease went busy (`QUIET=breach`) and were re-run after their round:
  round 4's scene-c arms 0 and 1, and round 5's scene-b arm 0. All 30 retained runs report
  `QUIET=guarded` and `RESULT=CLEAN`.

The scenes are rows (b) and (c) of [§ Scenes](#scenes). On this host scene (c)
is far more CPU-bound than on Metal. Inline, one fixed update takes about
35 ms, so every frame runs the catch-up cap of 8 updates.

| Scene | CPU / GPU ms per frame (serialized arm) |
|---|---:|
| (b) million control, GPU-bound | 1.75 / 30.16 |
| (c) CPU-bound | 294.96 / 52.28 |

### Three arms

Each cell is the mean of five rounds with its spread (largest round minus
smallest) in brackets. The spread is that row's noise bound.

| Scene | Arm | `input_to_present_ms` mean | p99 | `cursor_to_present_ms` mean | Steady frame mean ms | Updates / frame | Max frames in flight |
|---|---|---:|---:|---:|---:|---:|---:|
| GPU-bound | 0 (serialized) | 31.799 (2.416) | 33.402 (1.824) | 31.732 (2.417) | 32.034 (2.430) | 1.94 | 0 |
| GPU-bound | 1 (shipped) | 61.977 (0.785) | 63.654 (0.834) | 61.907 (0.786) | 31.094 (0.390) | 1.88 | 1 |
| GPU-bound | 2 (before) | 93.366 (0.272) | 95.297 (1.120) | 93.297 (0.260) | 31.184 (0.090) | 1.90 | 2 |
| CPU-bound | 0 (serialized) | 96.106 (5.579) | 112.029 (6.913) | 60.388 (6.568) | 347.360 (3.570) | 8.00 | 0 |
| CPU-bound | 1 (shipped) | 97.517 (9.420) | 111.892 (9.328) | 61.790 (8.487) | 292.826 (11.700) | 8.00 | 1 |
| CPU-bound | 2 (before) | 97.518 (9.208) | 111.711 (4.232) | 62.184 (9.074) | 290.740 (6.720) | 8.00 | 1 |

Readings:

- **GPU-bound: each frame in flight adds one GPU frame of latency.** Arm 1 reads
  30.2 ms above the serialized arm and arm 2 reads 61.6 ms above it. The
  serialized arm's spread is 2.4 ms. The steady frame mean improves by 0.9 ms
  (2.9%). This is the epic's known trap. The CPU half is 1.8 ms, so the next frame
  samples input right after `present()` returns and then queues behind the
  frame still executing.
- **CPU-bound: the bound moves throughput, not latency.** The three arms' latency
  means agree within 1.4 ms, against spreads of 5.6 to 9.4 ms. Serializing
  adds the GPU half to every frame: the frame mean rises 54.5 ms over arm 1.
  The GPU finishes each frame before the next present, so arm 2 never holds a
  second frame (maximum 1).
- **Arm 1 against arm 2**, the rule for landing the bound: no reading is worse
  beyond its noise. GPU-bound latency falls 31.4 ms (mean) and 31.6 ms (p99),
  and the frame mean moves 0.09 ms against spreads of 0.39 and 0.09 ms.
  CPU-bound, the p99 moves 0.18 ms and the frame mean 2.1 ms, against spreads of
  4.2 to 11.7 ms.
- **The shipped bound does not meet the latency rule at the GPU-bound scene.**
  Bound 1 still reads 30.2 ms above the serialized arm. A later start for the
  CPU frame would close that gap, and its policy is a design question shared
  with the Metal backend.
- **Clock mapping.** The CPU saw each end 0.10 to 0.12 ms after it (mean) where it
  waits on the fence: arm 0, and every GPU-bound arm. Where the end is
  only read at the next frame's `present()` (CPU-bound arms 1 and 2), the CPU sees
  it 230 to 245 ms late. The reading is still the GPU timestamp. The mapping error is at most
  0.78 ms per run, from the `GL_TIMESTAMP` read's round trip, against Metal's
  0.0015 ms.

Reports, summaries and manifests:
[`opengl/frames-in-flight/`](opengl/frames-in-flight/), one directory per
`round-<R>-<scene>-arm<N>`. Their `<worktree>` is the measuring pool worktree.

### Positive control on OpenGL

The [§ Positive control](#positive-control) pairs, light scene, Debug, three
interleaved pairs, at the default bound of 1. Every run reports
`QUIET=guarded` and 224 of 225 samples. The one unended frame is the frame
still in flight when the window closes. The mapping error is at most 1.30 ms.

| Pair | Without delay, mean ms | With delay, mean ms | Rise ms | Updates / frame without → with |
|---:|---:|---:|---:|---|
| 1 | 10.430 | 39.923 | 29.493 | 0.2 → 2.0 |
| 2 | 10.160 | 39.062 | 28.902 | 0.2 → 2.0 |
| 3 | 10.204 | 41.534 | 31.330 | 0.2 → 2.0 |

Every rise exceeds the injected 20 ms. The probe sees the sleep on OpenGL.
The extra 9 to 11 ms is the work behind the sleep. The delayed frames run two
fixed updates each, and their last poll precedes the last update. Also,
`cursor_to_present_ms` rises from 2.8 to 10.4 ms as `submit_to_present_ms` rises
from 0.6 to 7.1 ms. Reports: [`opengl/positive-control/`](opengl/positive-control/).

## Not measured here

- **OpenGL presentation time.** A visible OpenGL window also reports
  `gpu-complete`: the end of the frame's GPU work, not the swap's scan-out.
- **Presentation time.** A visible Metal window also reports `gpu-complete`.
  The drawable's presented time arrives later, on a Metal thread, and no
  unattended run has a drawable to exercise it.
