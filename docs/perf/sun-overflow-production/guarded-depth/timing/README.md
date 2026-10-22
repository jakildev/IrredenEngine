# Guarded source-face depth: provisional production timings

All 12 runs completed CLEAN inside an outer benchmark lease that exited 0 and
retained [GUARDED](quiet.txt). **These are provisional battery-powered results,
not qualified performance or a throughput target.** The user authorized continuing
despite unstable host power. Every recorded power sample in this particular run
says Battery Power; charge fell from 85% to 83%. The original
[matrix](matrix.json) preserves the complete raw before/after power reports for
each run. Quiet-window success does not establish stable clock or power behavior.

The guarded-depth build remains slower than both parent passes in the crowded
cases and the complete intermediate-camera case. Crowded cardinal frame means
are 70.60 ms versus 66.61/67.23 ms; intermediate-camera means are 71.96 ms versus
68.52/68.89 ms. These controls support investigating the remaining cost rather
than claiming parity. The much larger cost in the [initial matrix](../../README.md)
was measured separately under different conditions; comparing across those runs
is a diagnostic observation, not a qualified optimization ratio.

## All observations

Milliseconds, one 360-frame run per row, executed in table order. Steady frame
means exclude the first 90 frames and retain 270. GPU envelope and stage means
use their respective available samples, not that steady subset. Full reports
include percentiles, counts and tails; the summary's confidence bound is n/a for
one run. Both parent passes are retained without choosing the faster baseline.

| Pass | Case | Frame mean | Steady frame mean | GPU frame envelope | entityCanvasToFb |
|---|---|---:|---:|---:|---:|
| parent-a | complete-cardinal | 8.740 | 8.470 | 2.757 | 0.387 |
| parent-a | complete-oblique | 11.350 | 10.550 | 4.103 | 0.213 |
| parent-a | crowded-cardinal | 66.610 | 66.060 | 40.683 | 3.650 |
| parent-a | crowded-oblique | 68.520 | 67.630 | 41.839 | 3.843 |
| guarded-depth | complete-cardinal | 8.570 | 8.490 | 2.396 | 0.190 |
| guarded-depth | complete-oblique | 12.270 | 11.740 | 4.686 | 0.248 |
| guarded-depth | crowded-cardinal | 70.600 | 70.410 | 43.505 | 6.626 |
| guarded-depth | crowded-oblique | 71.960 | 71.030 | 44.077 | 5.285 |
| parent-b | complete-cardinal | 8.910 | 8.920 | 2.040 | 0.203 |
| parent-b | complete-oblique | 10.840 | 10.940 | 4.230 | 0.269 |
| parent-b | crowded-cardinal | 67.230 | 66.030 | 40.864 | 3.523 |
| parent-b | crowded-oblique | 68.890 | 67.910 | 42.035 | 3.846 |

GPU envelope spans the complete measured frame, including inter-submission gaps
and GPU stalls; it is not GPU busy time. `entityCanvasToFb` is the separately
reported stage measurement. Repeated private-canvas work is not completely
represented by every named stage row. Do not add the partial stage means to
estimate the full frame; this table keeps the full-frame envelope separate.
Every report has 360/360 valid GPU frame samples; stage samples number 359.

## Fixture and provenance

The exact [driver](driver.py.txt) runs parent-a → guarded-depth → parent-b, with
complete-cardinal, complete-oblique, crowded-cardinal, crowded-oblique within
each pass. Each run enables `IR_PERAXIS_SURFACE_LIGHTING=1`, disables AO, freezes
object and camera motion, and uses a source-face box above a GRID floor with
vertical sun. No screenshots, image readback or finite-geometry probe are in the
timed command. The crowded fixtures add 128 coincident distant casters and report
577 entities with many private canvases; they do not model one million entities.

“Cardinal” means camera yaw 0, zoom 1. “Oblique” means camera yaw 0.6 radians,
zoom 4; the sun remains vertical. Both request subdivision 1, with zoom-dependent
effective subdivision. Since camera yaw and zoom both change, these four cases
are not a factorial experiment isolating rotation cost or oblique sunlight.

Frozen artifact identities:

| Variant | Artifact base | Executable SHA-256 |
|---|---|---|
| Parent | `ffbcf2161820906760fce7299c035a141fe523b4` | `922ba74e19e17a66183dfa2814bc82dcd35e304b66bf2e213c0c5a507180cce0` |
| Guarded depth | `d832ed2ec5de4813e77a309bed0e1ab43719a346` plus retained source overlay | `ff093ee5053bb699b6a48bbc3dba08fddf8fe85b0a0631b98a4b4d02cebe8206` |

The [parent artifact](artifacts/parent/artifact.json) and
[guarded artifact](artifacts/guarded-depth/artifact.json) identify binaries,
shaders and runtime data. The guarded build's exact
[source overlay manifest](artifacts/guarded-depth/source/manifest.json), tracked
[engine patch](artifacts/guarded-depth/source/engine.patch) and eight new shader
files are retained together. An artifact base SHA alone does not identify this
uncommitted build. Per-run manifests record the separate integration checkout
`763a03a6020cc45af757602c8da7c75b11f7bb78` and its dirty state, not the frozen
executable's source identity. The driver hashes the frozen artifact files before
each variant; archive hashes preserve the reports and provenance.

Per-run quiet fields are `unknown` because the outer benchmark lease is reused;
the retained outer report is the quiet-window witness. Wrapper logs and original
text reports are losslessly gzip-compressed. Original run manifests, summaries
and matrix are unchanged. This archive introduces no new native Windows result,
full-scene performance qualification, default-route change or rendering proof;
see the separate [visual controls](../README.md) for image evidence.
