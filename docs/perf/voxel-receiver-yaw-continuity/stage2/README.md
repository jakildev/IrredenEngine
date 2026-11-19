# Voxel receiver yaw continuity, stage 2 — cost gate

The cost reading for the fog-pass half of
`docs/design/voxel-receiver-yaw-continuity.md`. It compares a parent tree
(the stack base `2a3bcd69f`) with the head tree (`b2e86e6e4`) in each tree's
**default configuration**: every `IR_PERAXIS_*` variable is unset on both arms
(every manifest's `render_environment` is all-null). The head's default is the
surface routes on every pipeline, with early depth rejection in the per-axis
scatter. The parent's default is stage 1: the surface routes only without a
fog pass.

- Host: Apple M4 Max, macOS, Metal, AC power, both trees Release.
- Every accepted run is `RESULT=CLEAN` and `QUIET=guarded`. Each run is one
  `scripts/perf/repeat_profile.py --repeats 1` launch, so each takes its own
  benchmark lease. A run that is not clean and guarded is retried, up to six
  tries.
- Ten runs were rejected and retried. Three on the million control wrote a
  report with 299 of the 300 requested frames, which `repeat_profile.py`
  rejects for a yaw sweep (it accepts it for a fixed pose). Seven on the
  fog-free matrix were `QUIET-BREACH` (exit 76) from another pane's dispatch.
  The fogged matrix had none.
- Five interleaved pairs per case, and ten or twenty for the cases named
  under the two shadowbox matrices. Case order runs forward on odd rounds and reversed on even rounds.
  Each case runs parent then head on odd rounds and head then parent on even
  rounds.
- The 1-minute load average at launch ranged from 2.6 to 5.5 for the million
  control, 2.5 to 11.0 for the fogged matrix and 2.6 to 8.8 for the fog-free
  one. Each launch first checks for a load below 12 and for
  `pgrep -f 'scripts/(render|light|gui)-verify\.py'` coming back empty, and
  waits up to 300 s for both; no launch had to wait.

The files:

- `driver.py.txt`: the interleaving driver, with its three suites.
- `grade.py.txt`: the grader that produced `*-grade.md`. Its last table names
  the surface-route stages each arm's reports sampled.
- `export.py.txt`: the exporter that produced `*-comparison.json`. Its
  `base_runs` and `head_runs` are paired by round, and each run carries its
  commit, binary hash, quiet verdict, load and battery.

## Gate

Rule (i) of `docs/design/gpu-stage-timing-cost-model.md` § "State the noise
bound": a frame row passes when its head mean is within 1.10 times the
parent mean. The `rule` column of the `*-grade.md` tables applies the same
ratio to every row it prints; only the `frame` and `steady` rows are gate
rows. A GPU stage row marked `FAIL(i)` there is a stage whose cost moved with
the route, not a gate failure.

### Million control (`IRPerfGrid`, 300 frames, `--wave-freeze --pivot-origin`)

All 12 frame and steady-frame rows pass. This is the first stage to move the
million control: the head samples `cardinalSurfaceFb` at yaw 0 and on the
sweep's crossings and `perAxisSurfaceFb` while rotating, and no parent run
samples either. The profiling-off preset times no stage on either arm.

| Case | Frame parent → head ms | Ratio | Steady ratio | GPU envelope parent → head ms |
|---|---:|---:|---:|---:|
| profiling on, yaw 0 | 18.436 → 18.588 | 1.008 | 1.009 | 17.719 → 17.881 |
| profiling on, yaw 45° | 10.886 → 11.286 | 1.037 | 1.038 | 9.720 → 10.167 |
| profiling on, sweep | 11.852 → 12.594 | 1.063 | 1.058 | 10.682 → 11.429 |
| profiling off, yaw 0 | 18.210 → 18.336 | 1.007 | 1.007 | 17.610 → 17.757 |
| profiling off, yaw 45° | 10.496 → 10.954 | 1.044 | 1.044 | 9.549 → 10.026 |
| profiling off, sweep | 11.490 → 12.200 | 1.062 | 1.057 | 10.498 → 11.248 |

The rotating rows' paired deltas are +0.40 to +0.74 ms with a standard
deviation of 0.04 to 0.09 ms, so the increase is real and small. Stage rows
on the rotating cases: `perAxisScatter` 0.50 to 0.59 ms and `computeSunShadow`
0.53 to 0.59 ms on the parent; `perAxisSurfaceFb` 1.80 to 1.83 ms and
`computeSunShadow` 0.03 ms on the head. `computeLightVolume` reads 0.32 ms on
the head against 0.23 ms.

### Fogged surface-lighting matrix (`IRCanvasStress`, shadowbox, `--fog-reveal 16`, 360 frames)

All 40 frame and steady-frame rows pass. Every case changes route on the
head except the two `-unlit` cases, which keep the compute route on both arms
because a debug overlay is a retained fallback.

| Case | Frame parent → head ms | Ratio | Steady ratio | GPU envelope parent → head ms | Surface stage on the head, ms |
|---|---:|---:|---:|---:|---:|
| cardinal zoom 1, no overlap | 8.458 → 8.496 | 1.004 | 1.006 | 2.658 → 2.859 | 1.637 |
| cardinal zoom 1, 8 casters | 9.730 → 9.810 | 1.008 | 1.012 | 5.273 → 5.245 | 0.575 |
| cardinal zoom 1, 16 casters | 10.868 → 10.968 | 1.009 | 1.006 | 6.323 → 6.555 | 1.090 |
| cardinal zoom 1, 8 casters, subdiv 4 | 9.904 → 10.382 | 1.048 | 1.055 | 5.963 → 6.118 | 0.906 |
| cardinal zoom 4, no overlap | 8.596 → 8.596 | 1.000 | 0.993 | 3.322 → 3.514 | 2.374 |
| cardinal zoom 4, 8 casters | 9.706 → 9.946 | 1.025 | 1.025 | 5.360 → 5.250 | 0.492 |
| cardinal zoom 4, 16 casters | 10.552 → 10.318 | 0.978 | 0.977 | 6.297 → 6.060 | 0.933 |
| cardinal zoom 4, 8 casters, subdiv 4 | 10.420 → 9.744 | 0.935 | 0.922 | 6.383 → 6.135 | 0.654 |
| cardinal zoom 4, 16 casters, shadows off (10 pairs) | 9.968 → 9.655 | 0.969 | 0.951 | 5.456 → 5.067 | 0.643 |
| cardinal zoom 4, 16 casters, unlit | 10.744 → 10.616 | 0.988 | 0.970 | 6.339 → 6.340 | — |
| per-axis zoom 1, no overlap | 9.362 → 9.286 | 0.992 | 1.003 | 4.953 → 4.837 | 1.665 |
| per-axis zoom 1, 8 casters | 10.586 → 10.252 | 0.968 | 0.981 | 6.332 → 6.064 | 1.497 |
| per-axis zoom 1, 16 casters | 10.726 → 11.208 | 1.045 | 1.036 | 6.560 → 6.758 | 1.389 |
| per-axis zoom 1, 8 casters, subdiv 4 | 10.942 → 11.428 | 1.044 | 1.058 | 6.466 → 6.561 | 1.230 |
| per-axis zoom 4, no overlap | 9.254 → 9.256 | 1.000 | 1.008 | 4.607 → 4.808 | 0.660 |
| per-axis zoom 4, 8 casters | 11.108 → 10.164 | 0.915 | 0.919 | 6.468 → 5.775 | 0.529 |
| per-axis zoom 4, 16 casters | 10.734 → 11.200 | 1.043 | 1.039 | 6.692 → 6.675 | 0.633 |
| per-axis zoom 4, 8 casters, subdiv 4 (10 pairs) | 10.306 → 11.107 | 1.078 | 1.076 | 6.280 → 6.445 | 0.520 |
| per-axis zoom 4, 16 casters, shadows off (10 pairs) | 10.830 → 10.312 | 0.952 | 0.948 | 6.240 → 5.769 | 0.205 |
| per-axis zoom 4, 16 casters, unlit | 10.816 → 10.596 | 0.980 | 0.984 | 6.651 → 6.687 | — |

**The ten-pair cases.** After five pairs, `per-axis-zoom4-overlap8-subdiv4`
read 1.113 on the frame row and 1.127 on the steady row, which fails the
point gate, with a paired delta of +1.13 ± 1.46 ms (one-sided 95% lower bound
−0.26 ms). Two other cases were as far out the other way
(`cardinal-zoom4-overlap16-no-shadows` 0.894 steady,
`per-axis-zoom4-overlap16-no-shadows` 0.901 steady). Every case with a frame
or steady ratio outside 0.91 to 1.10 after five pairs, in either direction,
took five more pairs (rounds 6 to 10). All ten pairs are in the comparison
file and in the means above. The three then read 1.078 / 1.076,
0.969 / 0.951 and 0.952 / 0.948.

**What the spread is.** The overlap cases sit near the pacing boundary, and
the frame follows the host's CPU state run by run. `RebuildGridVoxelsImplicit`
is an UPDATE system this change does not touch; in
`per-axis-zoom4-overlap8-subdiv4` its mean cost per tick reads 0.29 to
1.16 ms across runs of one binary, and each run's frame mean follows it on
both arms: 9.57 ms at 0.52 ms and 11.12 ms at 0.90 ms on the parent, 8.79 ms
at 0.29 ms and 11.01 ms at 0.92 ms on the head. Which state a run drew
decided its row, not which route it drew on.

**First-frame cost.** This matrix profiles from the first frame, so its frame
rows include start-up. The first two passes of this suite were discarded for
that: the head's first fog-composing frame took 1.5 s and its first cardinal
surface frame 0.29 s, because the fog-composing lit programs and the fog
pass's surface kernel compiled on first use. Both now compile at system
creation, and the tables above are from the tree with that change.

### Fog-free surface-lighting matrix (`IRCanvasStress`, shadowbox, 360 frames)

The third arm. The depth bound is in every scatter program, so it runs on
fog-free pipelines too. Both arms take the surface routes here (the stage 1
default): the per-axis cases are the ones whose shader changed, and the
cardinal cases run the same programs on both arms. All 40 frame and
steady-frame rows pass.

| Case | Frame parent → head ms | Ratio | Steady ratio | GPU envelope parent → head ms | Surface stage parent → head ms |
|---|---:|---:|---:|---:|---:|
| cardinal zoom 1, no overlap | 8.438 → 8.430 | 0.999 | 1.000 | 2.639 → 2.642 | 1.498 → 1.447 |
| cardinal zoom 1, 8 casters | 9.438 → 9.550 | 1.012 | 1.015 | 5.230 → 5.207 | 0.324 → 0.315 |
| cardinal zoom 1, 16 casters | 10.306 → 10.416 | 1.011 | 0.995 | 6.000 → 6.278 | 0.570 → 0.610 |
| cardinal zoom 1, 8 casters, subdiv 4 | 10.262 → 9.954 | 0.970 | 0.952 | 5.965 → 5.886 | 0.512 → 0.540 |
| cardinal zoom 4, no overlap | 8.524 → 8.470 | 0.994 | 0.994 | 2.950 → 2.923 | 1.760 → 1.858 |
| cardinal zoom 4, 8 casters | 9.962 → 9.728 | 0.977 | 0.968 | 5.381 → 5.343 | 0.257 → 0.254 |
| cardinal zoom 4, 16 casters (10 pairs) | 10.610 → 10.317 | 0.972 | 0.964 | 6.302 → 6.104 | 0.594 → 0.585 |
| cardinal zoom 4, 8 casters, subdiv 4 | 10.266 → 10.348 | 1.008 | 1.007 | 6.247 → 6.234 | 0.480 → 0.469 |
| cardinal zoom 4, 16 casters, shadows off | 9.756 → 9.574 | 0.981 | 0.991 | 5.293 → 4.842 | 0.224 → 0.199 |
| cardinal zoom 4, 16 casters, unlit | 10.262 → 10.098 | 0.984 | 0.974 | 6.292 → 6.162 | — |
| per-axis zoom 1, no overlap | 9.610 → 9.562 | 0.995 | 1.008 | 5.189 → 5.068 | 1.760 → 1.693 |
| per-axis zoom 1, 8 casters | 10.304 → 10.860 | 1.054 | 1.071 | 5.991 → 6.284 | 1.411 → 1.491 |
| per-axis zoom 1, 16 casters | 10.948 → 10.602 | 0.968 | 0.985 | 6.729 → 6.700 | 1.316 → 1.329 |
| per-axis zoom 1, 8 casters, subdiv 4 (10 pairs) | 11.052 → 10.988 | 0.994 | 0.988 | 6.546 → 6.480 | 1.253 → 1.226 |
| per-axis zoom 4, no overlap | 9.216 → 9.294 | 1.008 | 1.011 | 4.730 → 4.727 | 0.533 → 0.535 |
| per-axis zoom 4, 8 casters | 10.680 → 10.326 | 0.967 | 0.959 | 6.155 → 6.152 | 0.558 → 0.583 |
| per-axis zoom 4, 16 casters | 10.256 → 11.018 | 1.074 | 1.081 | 6.218 → 6.704 | 0.576 → 0.611 |
| per-axis zoom 4, 8 casters, subdiv 4 (20 pairs) | 10.516 → 11.151 | 1.060 | 1.073 | 6.295 → 6.521 | 0.488 → 0.493 |
| per-axis zoom 4, 16 casters, shadows off | 10.258 → 10.068 | 0.981 | 0.983 | 5.590 → 5.650 | 0.165 → 0.179 |
| per-axis zoom 4, 16 casters, unlit | 10.802 → 10.778 | 0.998 | 1.009 | 6.636 → 6.552 | — |

`perAxisSurfaceFb` reads 0.96 to 1.09 times the parent across the nine lit
per-axis cases. This fixture has little overdraw, so early depth has little
to reject here; its effect is on the million control.

**The ten-pair and twenty-pair cases.** The same rule as the fogged matrix.
After five pairs three cases were outside 0.91 to 1.10:
`cardinal-zoom4-overlap16` (0.887 steady), `per-axis-zoom1-overlap8-subdiv4`
(0.905 steady) and `per-axis-zoom4-overlap8-subdiv4` (1.143 frame, 1.175
steady). Five more pairs each gave 0.972 / 0.964, 0.994 / 0.988 and
1.096 / 1.120. The last still failed its steady row, so it took ten more:
1.060 / 1.073 over twenty pairs, 1.026 over the last ten. Every pair is in
the comparison file and in the means above.

**Control for `per-axis-zoom4-overlap8-subdiv4`.** It was the high case in
both shadowbox matrices, so it got a direct check
([flagged-case-control.json](flagged-case-control.json)): nine rounds, three
arms interleaved, fog-free, each run through `repeat_profile.py` and
`QUIET=guarded`.

| Arm | Frame mean ms | Steady mean ms |
|---|---:|---:|
| Parent binary | 11.516 | 11.241 |
| Head binary | 11.053 | 10.822 |
| Head binary with the parent's scatter shaders staged | 10.838 | 10.600 |

In that session the parent is the slowest arm. Together with the equal GPU
stage rows of the case (`perAxisSurfaceFb` 0.488 against 0.493 ms) and the
CPU-state reading under the fogged matrix, the case's early ratios were the
host drifting between CPU states across the session, not the executable and
not the depth bound.
