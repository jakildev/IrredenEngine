# Metal overlap: WIP measurements

This candidate does **not** satisfy the acceptance contract of issue 5000.
The pre-rebase 30-run matrix is retained below, including the failing
million-entity 0° block. The rebased continuation has one clean 0° signal
pair, not the required five interleaved pairs. It now has a valid exact-base
full render control/head pair, but the required Release offscreen matrix is
blocked by the inherited macOS launch-domain failure tracked in #5159. Do not
use the passing subset or a normal-window diagnostic to claim the task is
complete.

## 2026-11-26 rebased continuation

The candidate was rebased onto `f6d489ad6`, after render-pass batching from
PR 5100 merged. The conflict resolution preserves that batching while keeping
the overlap submission/resource-set lifetime rules. The current candidate is
`4d9947bf5`.

One fresh Release pair used the final binary and the million-entity 0°
commands below. It is a useful signal only:

| Reading | Blocking | Overlap |
|---|---:|---:|
| Steady frame mean | 18.888 ms | 18.677 ms |
| Steady frame p99 | 19.412 ms | 19.073 ms |
| `input_to_present` mean | 18.716 ms | 18.773 ms |
| `input_to_present` p99 | 19.182 ms | 19.314 ms |
| Steady samples | 225 | 225 |
| Frames starting with GPU work | 0 | 224 |
| Maximum frames in flight | 0 | 1 |
| Mid-frame waits | 0 | 0 |
| Render passes per frame | 3 | 3 |

Both arms recorded 300 valid GPU frames, no invalid samples, no command-buffer
errors and no skipped frames. The overlap arm's mean and p99 latency deltas
are within the 0.118 ms and 0.361 ms noise bounds. A second blocking Release
launch repeatedly failed before engine logging or a fresh profile, with macOS
launch services reporting `Connection Invalid` for
`com.apple.hiservices-xpcservice` and `_LSModifyNotification` error `-50`.
The same exact scene completed cleanly for 300 frames in Debug, and the first
Release pair completed cleanly. Failed launch outputs are not graded data.
The remaining interleaved 0°, 45° and CPU-bound runs therefore remain undone.

The display was online and awake for this continuation. The normal ShapeDebug
GUI suite passed 49/49; the presented-pick fault control failed only
`newest_completed_pick_frame` at `current=0/30`, as required. A fresh full
control at exact base `f6d489ad6` recorded 376 checks. Its five failures are
the #4904 baseline drift in LightingSdfBlocker, LuaPerfGrid and VoxelEditor.
The head comparison reproduced all five as inherited and moved no row, so its
control grade passed. CanvasStress passed 59/59, PerfGrid 35/35, ShapeDebug
187/187 and MetalClear 3/3 with maximum delta zero on the PR-critical rows.

The five-pair Release matrix could not start in the required unattended
offscreen mode. Two head launches and one separately rebuilt exact-base launch
all remained alive for 120 seconds without reaching engine logging or writing
a fresh profile. Each printed `Connection Invalid` for
`com.apple.hiservices-xpcservice` and `_LSModifyNotification` error `-50`.
The same head binary completed a five-frame offscreen probe and a 300-frame
normal-window probe with `QUIET=guarded` and `RESULT=CLEAN`; normal-window data
is not substituted for the commands-as-written offscreen acceptance method.
This exact awake-display launch-domain failure is tracked by #5159. Invalid
timeout outputs are not graded data.

Current functional verification:

| Check | Result |
|---|---|
| `fleet-build --target IrredenEngineTest` | succeeded |
| Focused Metal, presented-pick, input-latency, world-config, GPU-submission and render-pass tests | 103 tests in 15 suites passed; CLEAN |
| `fleet-jobs start light-verify -- --if-changed` | `20261126T050714Z-light-verify-80a732` succeeded, exit 0 |
| Normal ShapeDebug GUI verification | `20261126T071354Z-gui-verify-9bb526` succeeded, 49/49 |
| Presented-pick fault control | `20261126T071605Z-gui-verify-41bb2b` failed as expected, 48/49 |
| Exact-base full render control | `20261126T095840Z-render-verify-c221b4` recorded 376 checks; five #4904 baseline failures |
| Full head comparison | `20261126T112019Z-render-verify-21fbcd` succeeded; control grade PASS, five inherited, zero moved |
| `fleet-build --target format-changed` and header checks | succeeded; no new globals |
| `IRMetalClearTest --auto-screenshot 2` | CLEAN |

Permission-blocked diagnostics were recorded once and not retried:

- `ps -Ao pcpu,pid,etime,command | sort -nr | head -25` returned
  `zsh:1: operation not permitted: ps`.
- `sample IRPerfGrid 5 -file
  /private/tmp/fleet-pool-3/IRPerfGrid-release-hang.sample.txt` returned
  `Failure calling sysctl to get process list buffer size: Operation not
  permitted`; the process was not accessible to `sample`.

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

## Historical pre-rebase matrix

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

The sleeping-display blocker is resolved: this continuation observed
`online=1 awake=1 NSScreen=1`. The exact-base render control/head comparison is
also complete and passes its control grade. The remaining blocker is #5159:
both the PR head and exact base hang before engine logging when the required
300-frame Release profile uses the unattended offscreen path. Resume the full
interleaved performance blocks after that issue lands, and fix any remaining
performance failure before removing WIP. The existing ShapeDebug reference
tracker is issue 4904; it does not excuse an incomplete performance matrix.

Blocked operations recorded for the completion contract:

- `ps -p 61797 -o pid,ppid,comm` returned `operation not permitted` during
  quiet-lease diagnosis. It was not retried; no unknown process was killed.
- An initial `fleet-jobs start render-verify -- --all --record-control
  --tracker 4904 --json-out /private/tmp/fleet-pool-1/metal-overlap/render-parent.json`
  returned exit 2: the `--json-out` value is not allowed. The documented
  `build/` destination was used instead, producing the failed job above.

The two older blocked-operation rows above are retained as historical records.
The current permission-blocked commands are recorded in the rebased
continuation section.

Runtime/model for the rebased continuation: Codex / GPT-5. No merge or
review approval is claimed.
