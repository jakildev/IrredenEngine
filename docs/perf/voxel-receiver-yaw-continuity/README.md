# Voxel receiver yaw continuity — cost gate

The cost reading for `docs/design/voxel-receiver-yaw-continuity.md`. It
compares a parent tree (base `189e9f5e7`) with the head tree (`9382c4782`)
in each tree's **default configuration**: every `IR_PERAXIS_*` variable is
unset on both arms (every manifest's `render_environment` is all-null).

- Host: Apple M4 Max, macOS, Metal, AC power, both trees Release.
- Every accepted run is `RESULT=CLEAN` and `QUIET=guarded`. Each run is one
  `scripts/perf/repeat_profile.py --repeats 1` launch, so each takes its own
  benchmark lease. A run that is not clean and guarded is retried, up to three
  tries. Eleven runs were rejected, all `QUIET-BREACH` (exit 76): seven across
  six surface slots and four across three million slots.
- Five interleaved pairs per case. Case order runs forward on odd rounds and
  reversed on even rounds. Each case runs parent then head on odd rounds and
  head then parent on even rounds.
- The 1-minute load average at launch ranged from 2.0 to 25.9 for the
  surface matrix and from 2.2 to 7.5 for the million control. Other panes'
  builds were running. Each launch waited up to 300 s for a load below 12
  and for `pgrep -f 'scripts/(render|light|gui)-verify\.py'` to come back
  empty. Two accepted surface runs launched after that wait timed out: round
  1 of `cardinal-zoom1-overlap0` parent (load 36) and round 1 of
  `per-axis-zoom1-overlap8` head (a display verifier was live). Neither stands
  out from the same arm's other four rounds: 8.49 ms against 8.43 to 8.46,
  and 10.96 ms against 10.45 to 11.27.

The files:

- `driver.py.txt`: the interleaving driver.
- `grade.py.txt`: the grader that produced `*-grade.md`.
- `export.py.txt`: the exporter that produced `*-comparison.json`. Its
  `base_runs` and `head_runs` are paired by round, and each run carries its
  binary hash, quiet verdict, load and battery.

## Gate

Rule (i) of `docs/design/gpu-stage-timing-cost-model.md` § "State the noise
bound": a frame row passes when its head mean is within 1.10 times the
parent mean. Under stage 1, `IRCanvasStress` (fog-free) is the changed arm
and `IRPerfGrid`, which registers `FOG_TO_TRIXEL`, is the no-change arm.

### Fog-free surface-lighting matrix (`IRCanvasStress`, shadowbox, 360 frames)

All 40 frame and steady-frame rows pass. Only the per-axis cases change
route, from `perAxisScatter` + `lightingPerAxis` to `perAxisSurfaceFb`.
Cardinal cases keep `cardinalSurfaceFb` on both arms.

| Case | Frame parent → head ms | Ratio | GPU envelope parent → head ms |
|---|---:|---:|---:|
| per-axis zoom 1, no overlap | 8.708 → 9.164 | 1.052 | 3.912 → 5.233 |
| per-axis zoom 1, 8 casters | 10.578 → 10.920 | 1.032 | 6.247 → 6.556 |
| per-axis zoom 1, 16 casters | 10.798 → 10.844 | 1.004 | 6.712 → 6.625 |
| per-axis zoom 1, 8 casters, subdiv 4 | 10.718 → 10.690 | 0.997 | 6.487 → 6.581 |
| per-axis zoom 4, no overlap | 8.692 → 8.750 | 1.007 | 3.827 → 3.947 |
| per-axis zoom 4, 8 casters | 10.034 → 9.736 | 0.970 | 6.015 → 5.884 |
| per-axis zoom 4, 16 casters | 10.444 → 10.922 | 1.046 | 6.541 → 6.658 |
| per-axis zoom 4, 8 casters, subdiv 4 | 11.084 → 10.422 | 0.940 | 6.538 → 6.170 |
| per-axis zoom 4, 16 casters, shadows off | 10.098 → 9.846 | 0.975 | 5.915 → 5.686 |
| per-axis zoom 4, 16 casters, unlit | 10.470 → 10.648 | 1.017 | 6.634 → 6.406 |

The highest ratio in the matrix, 1.069 on the frame row and 1.095 on the
steady row, is on `cardinal-zoom4-overlap8`, whose route is the same on both
arms. That puts the run-to-run spread on this host at roughly ±7 to 10%.

**The GPU cost is real even though the frame rows pass.** The surface draw
replaces a cheaper scatter. Per-axis `perAxisSurfaceFb` reads 2.21 ms at
zoom 1 with no overlap, against 0.26 ms scatter plus 0.10 ms compute
lighting on the parent. The per-axis sun query (`computeSunShadow`) falls
from 0.30 to 0.05 ms. The envelope grows 1.34 times in that case. The frame
row moves only 1.05 times because the frame is paced near 8.3 ms and the GPU
work fits inside it. Each paired delta is positive and consistent
(+0.456 ± 0.267 ms, one-sided 95% lower bound +0.20 ms). With casters, the
envelope is within ±6% of the parent at both zooms.

### Million control (`IRPerfGrid`, 300 frames, `--wave-freeze --pivot-origin`)

All 12 frame and steady-frame rows pass. The arms register the same GPU
stage set: `fogToTrixel` is present on both, and `perAxisSurfaceFb` is
present on neither.

| Case | Frame parent → head ms | Ratio |
|---|---:|---:|
| profiling on, yaw 0 | 29.550 → 28.930 | 0.979 |
| profiling on, yaw 45° | 43.278 → 41.944 | 0.969 |
| profiling on, sweep | 48.156 → 47.988 | 0.997 |
| profiling off, yaw 0 | 29.158 → 28.840 | 0.989 |
| profiling off, yaw 45° | 40.704 → 42.810 | 1.052 |
| profiling off, sweep | 48.044 → 48.722 | 1.014 |

The profiling-off yaw 45° row spans 40.0 to 43.2 ms on the parent and 40.0
to 46.1 ms on the head. Its paired lower bound is −0.85 ms, which is noise
on a route stage 1 does not change.
