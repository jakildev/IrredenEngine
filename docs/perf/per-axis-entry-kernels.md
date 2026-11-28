# Per-axis list kernels: one invocation per entry

The per-axis store, winner election, colour pass and overflow append each run
as their own compiled kernel (`IR_PER_AXIS_PASS 1`) and launch one invocation
per entry of the axis list, instead of the general stage kernels' 2×3×N voxel
lattice selected by `perAxisRoute` and `resolveMode` at run time. The
mechanism is in
[per-axis-trixel-canvas-rotation.md](../design/per-axis-trixel-canvas-rotation.md)
§ "Current contract", "Per-axis kernels".

Host for every number: Apple M4 Max, Metal, macOS, Release, AC power, with
other fleet panes live. Base is master `f6d489ad6`, which carries #5101's
store sizing and drop counter and #5121's cross-axis AO; head is this change
rebased onto it. Every run is a 300-frame `repeat_profile.py` run under the
benchmark lease; a pair counts only when both runs read `QUIET=guarded`. Base
and head alternate, reversed on even rounds. OpenGL was not measured.

## Launched invocations

`IRPerfGrid --wave-freeze --pivot-origin --no-overlay --yaw 0.785398163`
(64³, Debug), one indirect-args readback per axis on a rotating frame, taken
on base `4303d18ae` before the rebase (the rebase changed no dispatch code):

| Arm | Entries per axis | Groups per axis | Threads per group | Invocations per entry |
|---|---:|---:|---:|---:|
| base | 262,144 | 1024 × 8 × 1 | 192 (2×3×32) | 6 |
| head | 262,144 | 1024 × 4 × 1 | 64 | 1 |

The dispatch count per rotating frame is unchanged (12 list dispatches: the
election and append are separated by the occluder prepass).

## Frame cost

Million control: `IRPerfGrid --wave-freeze --no-overlay --pivot-origin
--auto-profile 300 --config-preset configs/perf/million-profiling-off.lua`.

| Case | Pairs | Base envelope ms | Head envelope ms | Mean reduction ms | One-sided 95% lower bound ms |
|---|---:|---:|---:|---:|---:|
| million, 45° | 5 | 9.943 | 7.689 | 2.254 | 2.182 |
| million, 0° | 5 | 17.890 | 17.505 | 0.385 | 0.358 |
| 64³ default, zoom 1, 45° | 5 | 7.497 | 6.947 | 0.550 | 0.521 |

One 45° base run lost its quiet guard and was re-run in the same round. The
cardinal 0° frame gains because the general kernels now compile with no
per-axis code. Every pair moved the same way in all three cases (per-pair
readings in the PR's evidence table).

## Stage rows, million control at 45°

`configs/perf/million.lua` (stage timers on), three pairs:

| Row | Base ms | Head ms | Delta ms |
|---|---:|---:|---:|
| `voxelPerAxisStore` | 0.846 | 0.257 | −0.589 |
| `voxelPerAxisFinalize` | 1.396 | 0.570 | −0.826 |
| `voxelPerAxisOverflow` | 2.447 | 2.274 | −0.173 |
| `perAxisCellCompact` | 0.170 | 0.163 | −0.007 |
| GPU frame envelope | 10.463 | 8.890 | −1.574 |

The rows are not additive with the envelope; they bound where the saving lands.
The overflow witness is the same in all eleven counted 45° runs, base and
head: candidates 2,364,165, unsampled 668,580, hidden 1,643,866, surviving
51,719, overflow `maxDropped=0`, store `maxDropped=0`.
