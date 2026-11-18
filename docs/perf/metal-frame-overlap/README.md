# Metal overlap: WIP measurements

This candidate does **not** satisfy the acceptance contract of issue 5000.
The complete final matrix is retained, including the failing million-entity
0° block. The full render comparison is also incomplete because the display
went to sleep during the parent control. Do not use the passing subsets to
claim the task is complete.

## Provenance and method

- Source: `226c7b22e`, parent `6236f5854df959f532add8b23645580318e09a18`.
  Manifests name `c8c9aaec9` with four dirty files; those exact changes became
  `226c7b22e`. Subsequent changes only record documentation and evidence.
- Apple M4 Max, macOS 26.5.2, Metal, AC power, battery 58–60%, offscreen
  120 Hz pacing. No thermal or performance warning was reported by `pmset`
  after the matrix; this does not establish why GPU cost changed during it.
- Release, stage profiling off, one binary for all 30 final runs:
  SHA-256 `7d04dd636a7127635ae72b67100e81b4e888ba6d5da4830e1fb765250388fada`.
- Five interleaved off/on pairs per scene, in order: million 45°, million
  0°, CPU-bound. Every run reported `QUIET=guarded`, `RESULT=CLEAN`, exit 0.
  The blocking preset loads the matching scene preset and only changes
  `metal_frame_overlap` to false. `base` below means that same-binary arm,
  not a different parent executable.
- Each run requests 300 frames. Reports contain 299 or 300 frame records;
  steady frame/latency statistics discard the leading quarter (74 or 75),
  leaving 225. Frame mean includes pacing; CPU is the probe's
  `frame_start_to_submit_ms`; GPU is actual `commandBufferSpans` execution
  time from the whole-window GPU report. The probe's submit-to-end interval
  is preserved in each raw report but is not used as GPU execution cost.
- Tables average the five per-run means and the five per-run p99 values,
  matching the [baseline noise-bound table](../input-latency/README.md#noise-bound).
  Frame p99 indexing is `min(n * 99 / 100, n - 1)` with integer division.
  Pick age, stale counts and updates/frame cover the full recorded window.
- [comparison.json](comparison.json) contains each run and the aggregates.
  [raw/](raw/) contains all manifests, stdout and profiles. Only absolute
  local paths have been replaced by `<worktree>`, `<engine-root>` or `<home>`
  in the raw profiles. Verifier logs also trim trailing whitespace.

## Commands

Run the harness bare so it retains the display domain. Each profile run
takes its own benchmark lease; do not wrap the matrix in an outer lease.
For each of five pairs, run the blocking preset followed by the enabled
preset and use a distinct output directory. The final directories are
`raw/<million0|million45|cpu>/<base1|head1|...|base5|head5>`.

```bash
python3 scripts/perf/repeat_profile.py --release --target IRPerfGrid --repeats 1 --output <output> -- --auto-profile 300 --config-preset configs/perf/million-profiling-off-blocking.lua --yaw 0 --wave-freeze --no-overlay --pivot-origin
python3 scripts/perf/repeat_profile.py --release --target IRPerfGrid --repeats 1 --output <output> -- --auto-profile 300 --config-preset configs/perf/million-profiling-off.lua --yaw 0 --wave-freeze --no-overlay --pivot-origin
```

Repeat those commands with `--yaw 45` for the second million pose. These
presets use 1,048,576 entities, zoom 4, frozen wave, origin pivot and the
same default lighting/shadows in both arms; the full report records the
visible population. For the CPU-bound 64³ per-cell wave scene:

```bash
python3 scripts/perf/repeat_profile.py --release --target IRPerfGrid --repeats 1 --output <output> -- --auto-profile 300 --config-preset configs/perf/profiling-off-blocking.lua --yaw 0 --no-overlay --pivot-origin --worker-threads 0
python3 scripts/perf/repeat_profile.py --release --target IRPerfGrid --repeats 1 --output <output> -- --auto-profile 300 --config-preset configs/perf/profiling-off.lua --yaw 0 --no-overlay --pivot-origin --worker-threads 0
```

## Final matrix

All time values are milliseconds. The ratio is frame / max(CPU, GPU).

| Scene | Overlap | Frame mean | CPU mean | GPU mean | Per-run ratio range | Updates/frame |
|---|---|---:|---:|---:|---:|---:|
| Million 0° | off | 21.993 | 0.567 | 20.543 | 1.035–1.107 | 1.276 |
| Million 0° | on | 24.494 | 0.787 | 23.109 | 1.004–1.116 | 1.422 |
| Million 45° | off | 10.745 | 0.903 | 9.697 | 1.105–1.110 | 0.646 |
| Million 45° | on | 10.550 | 0.936 | 9.705 | 1.081–1.092 | 0.635 |
| CPU-bound | off | 17.915 | 13.261 | 4.161 | 1.328–1.364 | 1.133 |
| CPU-bound | on | 17.980 | 13.205 | 4.251 | 1.350–1.387 | 1.121 |

The 45° enabled arm satisfies the 1.10 ratio in every run. At 0°, pair 4's
enabled ratio is 1.116, so criterion 1 is not demonstrated consistently.
The CPU-bound scene deliberately uses the serialized fallback; the million
throughput criterion does not apply to that scene.

| Scene | Input mean off → on | Input p99 off → on | Mean delta / bound | p99 delta / bound | Latency gate |
|---|---:|---:|---:|---:|---|
| Million 0° | 21.855 → 24.680 | 24.412 → 28.032 | +2.825 / 0.118 | +3.620 / 0.361 | FAIL |
| Million 45° | 14.461 → 14.549 | 21.670 → 21.547 | +0.088 / 0.118 | −0.123 / 0.361 | PASS, additional pose |
| CPU-bound | 17.126 → 17.167 | 21.004 → 20.926 | +0.041 / 0.140 | −0.078 / 2.119 | PASS |

The mandated GPU latency scene is 0°, per plan correction A1. Applying the
same bound to 45° is additional evidence, not a substitute for that scene.

| Scene | Enabled steady frames starting with GPU work, pairs 1–5 | Disabled | Maximum at frame start, off/on | Mid-frame drains, off/on |
|---|---|---|---|---|
| Million 0° | 225, 225, 187, 164, 139 of 225 | 0 each | 0 / 1 | 0 / 0 |
| Million 45° | 225 of 225 each | 0 each | 0 / 1 | 0 / 0 |
| CPU-bound | 0 each, serialized fallback | 0 each | 0 / 0 | 0 / 0 |

The 0° positive-fire gate fails: the last three runs achieve 83.1%, 72.9%
and 61.8%, below 90%. GPU execution cost rises from about 17.6 ms to 24–29 ms
in later runs of both arms. The cause was not established. The quiet guard
held, so those runs remain in the graded data. A complete 0° recheck was
planned but not launched after the parent render job established that the
display was asleep. No favorable subset replaces the failed block.

## Pick age (reported, not a gate)

| Scene | Pick age mean off → on | Pick age p99 off → on | Stale INPUT ticks off/on |
|---|---:|---:|---:|
| Million 0° | 21.569 → 42.565 | 24.549 → 55.392 | 0 / 0 |
| Million 45° | 10.731 → 21.046 | 11.135 → 21.599 | 0 / 0 |
| CPU-bound | 11.105 → 10.275 | 68.636 → 43.992 | 0 / 0 |

The GPU-bound pick record is roughly one extra GPU frame old under overlap.
This is expected: the served completed-frame record precedes the executing
submission. The CPU fallback avoids that rise in the CPU-bound fixture.

## Experiments informing the candidate

- [Always-overlap CPU experiment](experiments/always-overlap-cpu/comparison.json):
  five guarded pairs increased mean input latency by 1.638 ms against the
  0.140 ms bound. Reduced updates/frame caused more rendered frames to use
  a preceding fixed-update poll. This rejected prototype motivated keeping
  blocking present when predicted CPU cost is at least predicted GPU cost.
- [Uncapped 30% lead, 45° experiment](experiments/uncapped-lead-yaw45/comparison.json):
  mean latency rose 0.203 ms, beyond the additional pose's 0.118 ms bound.
  The final candidate caps the prediction lead at 0.15 ms. The two-ms active
  pacing tail consumes CPU; power cost has not been measured.

These experiments used other binaries and are not mixed into the final matrix.

## Functional and capture evidence

All functional checks below used the final candidate's code. Job results are
terminal `fleet-jobs wait` observations, not launch acknowledgments.

| Check | Terminal result |
|---|---|
| Release build `20261118T021040Z-build-9737e2` | succeeded, exit 0 |
| Debug test build `20261118T021040Z-build-e33df0` | succeeded, exit 0 |
| `fleet-run --timeout 120 IrredenEngineTest '--gtest_filter=Metal*:*PresentedPick*:*InputLatency*:*WorldConfigMetalOverlap*:*ProfileReport*'` | 90 tests, 19 suites passed; CLEAN, exit 0 |
| `fleet-jobs start light-verify -- --if-changed` | `20261118T022104Z-light-verify-6c7393` succeeded, exit 0; all checks PASS |
| `fleet-jobs start gui-verify -- IRShapeDebug -- --gui-test` | `20261118T022417Z-gui-verify-d642aa` succeeded, exit 0; 49/49 |
| Same GUI command with `--config-preset configs/presented-pick-previous.lua` | `20261118T022453Z-gui-verify-4f745f` failed, exit 1 as expected; 48/49, only newest completed pick fails at `current=0/30` |

The five `MetalFrameOverlapTest` cases cover executing-buffer preservation,
separate staging lifetimes, ordered 2D/3D uploads, destruction while native
resources are retained by submitted commands, and six no-drawable frames
with a live recording command buffer and at most one submitted buffer.

Two clean committed refs, parent `6236f5854` and candidate `226c7b22e`:

```bash
fleet-run IRCanvasStress --frozen-pose 0.47 --sweep-yaw 0 6.2831853 9 --auto-screenshot 6
fleet-run IRCanvasStress --auto-record 180 --config-preset <worktree>/scripts/fleet/clip-preset.lua
```

The nine captures use 1280×720, zoom 1, default CanvasStress population,
lights and shadows, frozen pose 0.47 and the nine prescribed camera yaws.
Every pair is RGB-identical: 100% match, max delta 0, PSNR infinite, using
`scripts/render-compare.py --per-pixel-tol 0 --threshold-match-pct 100`.
The 27 normalized screenshot/camera/filename records match. The animated
recording uses the clip preset (640×360, 30 fps); each ref runs 180 update
ticks, and `ffmpeg -f framemd5` produces identical timestamps and decoded
hashes for all 90 frames. All four capture launches exit CLEAN.

Logs and comparison records are in [validation/](validation/). Media is
pinned at `df5c2c53e395730bd4a209872f80e3bc37a4e58d`:
[nine pairs](https://github.com/jakildev/IrredenEngine/tree/df5c2c53e395730bd4a209872f80e3bc37a4e58d/docs/pr-screenshots/claude/5000-metal-overlap),
[side-by-side clip](https://raw.githubusercontent.com/jakildev/IrredenEngine/df5c2c53e395730bd4a209872f80e3bc37a4e58d/docs/pr-screenshots/claude/5000-metal-overlap/IRCanvasStress-clip.mp4).

## Blocked validation and continuation

`fleet-jobs start render-verify -- --all --record-control --tracker 4904
--json-out build/metal-overlap-render-parent.json` at clean parent `6236f5854`
created job `20261118T023223Z-render-verify-3913a0`. Its terminal wait was
`failed exit=1`. It reached the CanvasStress extra runs, then reported
`CoreGraphics online=1 awake=0 NSScreen=1; the engine will discover 0 monitors`.
CanvasStress lacked the expected captures; subsequent scenes exited 69.
This is not a valid complete parent control. A head `--against-control` run
was not launched. The earlier attempt on a pre-rebase tree also failed with
NO-DISPLAY; neither attempt is counted as passing render evidence.

The host must be awake in its logged-in Aqua session before continuing.
Then rebase/sequence the overlapping render-pass PR as required by the fleet
overlap gate, rebuild, obtain the exact merge-base render control, run the
head comparison, and repeat complete interleaved performance blocks. Fix any
remaining performance failure before removing WIP. The existing ShapeDebug
reference tracker is issue 4904; it does not excuse an incomplete control.

Blocked operations recorded for the completion contract:

- `ps -p 61797 -o pid,ppid,comm` returned `operation not permitted` during
  quiet-lease diagnosis. It was not retried; no unknown process was killed.
- An initial `fleet-jobs start render-verify -- --all --record-control
  --tracker 4904 --json-out /private/tmp/fleet-pool-1/metal-overlap/render-parent.json`
  returned exit 2: the `--json-out` value is not allowed. The documented
  `build/` destination was used instead, producing the failed job above.

Runtime/model: Codex / GPT-6. No merge or review approval is claimed.
