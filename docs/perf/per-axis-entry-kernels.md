# Per-axis list kernels: one invocation per entry

The per-axis store, winner election, colour pass and overflow append each run
as their own compiled kernel (`IR_PER_AXIS_PASS 1`) and launch one invocation
per entry of the axis list, instead of the general stage kernels' 2×3×N voxel
lattice selected by `perAxisRoute` and `resolveMode` at run time. The
mechanism is in
[per-axis-trixel-canvas-rotation.md](../design/per-axis-trixel-canvas-rotation.md)
§ "Current contract", "Per-axis kernels".

Host for every number: Apple M4 Max, Metal, macOS, Release, AC power, with
other fleet panes live. Base is master `4303d18ae`; head is this change on top
of it. Every run is a 300-frame `repeat_profile.py` run under the benchmark
lease; a pair counts only when both runs read `QUIET=guarded`. Base and head
alternate, reversed on even rounds. OpenGL was not measured.

## Launched invocations

`IRPerfGrid --wave-freeze --pivot-origin --no-overlay --yaw 0.785398163`
(64³, Debug), one indirect-args readback per axis on a rotating frame:

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
| million, 45° | 5 | 10.611 | 8.868 | 1.743 | 1.461 |
| million, 0° | 5 | 18.391 | 18.060 | 0.331 | 0.318 |
| 64³ default, zoom 1, 45° | 5 | 8.495 | 8.815 | −0.320 | −0.877 |

A sixth 45° pair lost its head run to a quiet breach. The cardinal 0° frame
gains because the general kernels now compile with no per-axis code.

The 64³ zoom-1 frame is light (about half the 16.7 ms vsync budget) and its
envelope spread is wide (7.65 to 9.54 ms on base, 8.15 to 9.89 on head). Its `frame avg`
row is 16.67 ms on both arms and the envelope ratio is 1.04x. Rows this change
does not touch moved with it in all five pairs (`trixelToFb` +0.63 ms,
`computeSunShadow` +0.29, `perAxisScatter` +0.17), while the same rows are
flat on the GPU-bound million control (within 0.01 ms; `trixelToFb` fell
0.10); the touched rows fell (`voxelPerAxisStore` −0.18, `voxelPerAxisFinalize`
−0.50). The likeliest reading of the 64³ envelope is host and GPU clock
state rather than added work, but these runs do not prove it; they are not
evidence of a saving there either.

## Stage rows, million control at 45°

`configs/perf/million.lua` (stage timers on), three pairs:

| Row | Base ms | Head ms | Delta ms |
|---|---:|---:|---:|
| `voxelPerAxisStore` | 0.733 | 0.251 | −0.482 |
| `voxelPerAxisFinalize` | 1.337 | 0.490 | −0.847 |
| `voxelPerAxisOverflow` | 2.386 | 2.307 | −0.079 |
| `perAxisCellCompact` | 0.073 | 0.072 | −0.001 |
| GPU frame envelope | 10.932 | 9.549 | −1.383 |

The rows are not additive with the envelope; they bound where the saving lands.
The overflow witness is the same on both arms at 45°: candidates 2,364,165,
unsampled 668,580, hidden 1,643,866, surviving 51,719, `maxDropped=0`.
