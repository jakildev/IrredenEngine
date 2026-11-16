# Fog reveal cadence control

The million-entity control shows why voxel BODY reveal needs a contiguous-block
window rather than the existing per-entity stride. A stride lowers the number of
verdicts but still touches the 200-byte voxel-set row at scattered addresses. The
window returns before that row is read and keeps the evaluated blocks contiguous.

## Control

Release Metal on an Apple M4 Max, 1280×720, November 16, 2026. Each row is a
300-rendered-frame yaw sweep over the one-million-cell
`million-profiling-off.lua` fixture. `Updates/frame` is the evaluator call count
divided by 300. The representative rows use one guarded run per arm.

| Cadence | `FogRevealEval` mean/call | Evaluated subjects/update | Updates/frame | Frame mean |
|---|---:|---:|---:|---:|
| period 1 | 1.791 ms | 1,000,000.0 | 2.80 | 47.33 ms |
| strided 4 | 1.853 ms | 250,000.0 | 3.02 | 51.18 ms |
| strided 16 | 1.773 ms | 62,500.0 | 2.95 | 49.95 ms |
| window 4 | 0.574 ms | 249,999.4 | 2.55 | 43.14 ms |
| window 16 | 0.335 ms | 62,542.2 | 2.76 | 46.76 ms |

The strided lever is refuted on this population: neither strided arm reduces
the evaluator cost. Window 4 is 0.320 of period 1 in the representative pair.
Three interleaved pairs read 0.574/1.791, 0.629/1.689, and 0.674/1.886 ms,
ratios 0.320, 0.372, and 0.357. Every pair is below the 0.60 gate. Window 4's
evaluated count is 24.9999% in the first two rounds and 25.0017% in the third.

## Cadence contract

`fog_window_period = W` selects one 4096-entity-id block residue per update.
Every BODY subject is fully evaluated once in any `W` consecutive updates, so
a movement, canvas change, or gate disagreement can remain stale for at most
`W` updates. Overrides remain next-update operations. When the window is 1,
`fog_stagger_period = P` retains the original per-entity stride and its
`P`-update bound.

When both controls are non-default, the window takes precedence for voxel BODY
subjects. Ghost, shape, and canvas evaluators ignore the window and continue to
use the strided period. Applying both predicates to the voxel evaluator would
starve three quarters of consecutive ids at `P = W = 4`; precedence avoids that
schedule.

## Commands and fingerprints

Each arm used:

```bash
ir-acquire benchmark -- python3 scripts/perf/repeat_profile.py \
  --target IRPerfGrid --release --output <arm-dir> --repeats 1 -- \
  --wave-freeze --no-overlay --pivot-origin --auto-profile 300 \
  --config-preset configs/perf/million-profiling-off.lua \
  --yaw 0 --yaw-step 0.020943951 <cadence flags>
```

The period-1 arm used `--fog-window-period 1`; window arms used
`--fog-window-period 4|16`; strided arms used `--fog-stagger-period 4|16
--fog-window-period 1`. Every retained run exited `RESULT=CLEAN`; the benchmark
lease completed without a quiet breach for the rows above.

- Source base: `345dbbf251fe6b78a2e5af80c0c24479493d664f`
- Release binary SHA-256: `73c786026e0bb5f0bc7692627c06488457d31e49de0c1ddd94ba6de8d035b920`
- Shader SHA-256: `8ef66350f428ea9a5835a69b7058e557a456f97e0c30aff8ab5a970dea2743d3`
- Runtime scripts SHA-256: `85a4c4c0e04ff77ce830640f6b8935b4ab8dcad11f1ff0dfccbc3dd918d927f6`

The compact retained readings are in
[`fog-reveal-stagger/measurements.csv`](fog-reveal-stagger/measurements.csv).
