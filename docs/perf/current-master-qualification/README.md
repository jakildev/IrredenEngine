# Incomplete Release performance qualification

**Outcome: not qualified.** Two older fixed-pose runs were individually guarded;
the sweep was contaminated, and both subsequent admission attempts were refused.
No new benchmark was admitted for the current stack. No speedup, performance-equivalence or 60-fps claim follows.
This record covers Metal on the Apple M4 Max; it does not qualify OpenGL.

## Evidence and admission

| Capture | Revision | Power | Guard / exit | Disposition |
|---|---|---|---|---|
| Cardinal round 1 | `9256ac047` | Battery, 63% | guarded / 0 | Partial diagnostic evidence |
| 45° round 1 | `9256ac047` | Battery, 60% | guarded / 0 | Partial diagnostic evidence |
| Sweep round 1 | `9256ac047` | Battery, 58% | breach / 76 | Reject timing conclusions |
| Cardinal retry | `44e89a222` | AC | refused / 75 | No fresh report; no timing |
| Cylinder baseline | `7df2843fd` + fixture changes | AC | refused / 75 | Engine never launched; no timing |

[Original matrix](partial-9256ac047/cases.json) requested three rounds of
cardinal/45°/sweep, with profiling on/off: 18 runs. It stopped on run 3; no
profiling-off arm or repeated round completed. The `current-master-million`
source name refers to its launch-time revision, before the subsequent merge.

The [retry manifest](refused-44e89a222/release-profiling-on-yaw0/round-1/manifest.json)
records `fresh_report=false`, `clean=false`, `quiet=refused`. It waited from
19:49:35 to 19:59:36 UTC on October 13, 2026, then reported HOST-NOT-QUIET.
The busy lease's 1081-second age is not engine runtime. Admission was never
obtained, so this retry contributes no performance samples.

The [cylinder baseline manifest](refused-cylinder-7df2843fd/manifest.json) and
[launcher log](refused-cylinder-7df2843fd/run-1.log) record another refusal on
October 13, 20:17:04–20:18:54 UTC: exit75, `quiet=refused`, `fresh_report=false`,
`clean=false`, `engine_logged=false`. This Release capture/profile command
correctly requested `ir-acquire benchmark`, confirming #4404's route selection.
The checkout had CanvasStress fixture changes atop `7df2843fd`; it was not a
clean master baseline. No before/after cost comparison exists. Neither wait
elapsed time nor the busy lease's 371-second age is an engine measurement.

## Partial fixed-pose measurements

Release `IRPerfGrid`, 100³ single-voxel entities in pool128, zoom4, full
subdivisions/base1, frozen wave, origin pivot, no overlay, scope/stage profiling
on. The report counts 1,000,186 total ECS entities, including infrastructure.

| Older guarded run | Warmed wall mean / p95 / p99 (ms) | Full-run GPU envelope mean (ms) | Updates/rendered frame |
|---|---:|---:|---:|
| Cardinal | 47.528 / 48.987 / 49.818 | 17.819 | 2.9 |
| 45° | 56.124 / 59.717 / 60.146 | 24.143 | 3.4 |

Each warmed wall distribution excludes 300 of 1200 frames, leaving 900 samples.
GPU envelopes cover all 1200 valid frames, including startup; they include
inter-submission gaps, and command-buffer spans include stalls. Neither is GPU
busy time. GPU stage rows are full-run sampled invocation means (1199 samples
in each fixed pose); **they are not additive frame budgets**. CPU system means
also include startup and divide by calls, not warmed frames. No call-stack
samples or peak process/GPU residency measurements were collected.

Raw [cardinal report](partial-9256ac047/release-profiling-on-yaw0/round-1/run-1.txt.gz)
and [45° report](partial-9256ac047/release-profiling-on-yaw45/round-1/run-1.txt.gz)
retain complete tables and frame sequences. The rejected sweep remains under
`partial-9256ac047/release-profiling-on-yawsweep/`, with its breach manifest/log;
do not pool its samples with accepted runs.

## Investigation targets, not claimed savings

- At 45°, 909,358 candidates produce 2,728,074 axis entries; overflow reaches
  2,652,369 records with zero dropped. Cardinal retains 786,156 candidates plus
  110,916 feeders. Roughly 90% of the pool remains in either route; this does not
  establish a million individually visible objects. Overflow/order/scatter are
  useful targets: sampled 45° overflow and scatter means are 11.594/5.543 ms.
- Sampled light-volume means are 16.584/14.769 ms (cardinal/45°); per-axis AO is
  14.107 ms at 45°. These cannot be subtracted from frame time. The earlier
  [GPU attribution study](../gpu-cost-attribution.md) shows why full-frame
  controlled ablations, rather than stage rankings alone, must guide changes.
- CPU `SingleVoxelToCanvasFirst` averages 4.546/4.782 ms per rendered call;
  `BuildLightOcclusionGrid` 1.590/1.536 ms. Per-update `UpdateVoxelSetChildren`
  is 1.555/1.553 ms, `FogRevealEval` 1.543/1.530 ms, and `PropagateTransform`
  0.791/0.773 ms. There are 3425/4049 updates in 1200 frames. Audit repeated
  whole-population work and catch-up while preserving simulation/FOW semantics.

## Shadow-index controls and provenance

The [committed bounded box controls](../../pr-screenshots/codex/shadow-index-complete-tiles/cost-controls/README.md)
are linked rather than duplicated. All 24 runs have `quiet=unknown`; they include
screenshot readback, pacing and grouped battery-powered sessions. Large-span
warmed means vary from 10.95–15.19 ms before and 10.20–13.91 ms after.
[PR #4404](https://github.com/jakildev/IrredenEngine/pull/4404) fixes the launcher
ordering that downgraded profiling followed by capture to GPU-only acquisition.
That fix cannot retroactively qualify those timings. The 2,228,228-byte added
index allocation is buffer capacity, not measured full GPU residency or RSS.

[Evidence inventory](evidence.json) records original source paths, byte counts
and SHA256 hashes. Reports and manifests are preserved byte-for-byte (reports
are gzip-compressed); manifests include executable, shader and script hashes.
Only launcher-log absolute engine/worktree prefixes are replaced with symbolic
`${ENGINE_ROOT}`/`${WORKTREE}` paths to omit local account names. Original and
exported hashes record this normalization. No engine measurements were rerun
while assembling this document.

## Remaining qualification

Obtain a quiet slot without bypassing guards. Start with one current Release
45° profile-off run at 600 frames, then its profile-on match, on AC with identical
pacing; a short `IR_QUIET_DRAIN=30` can fail admission promptly without weakening
it. Require guarded, clean, fresh reports and correct pose/overflow witnesses.
Then complete repeated cardinal/45°/sweep, followed by fixed-density zoom/motion
controls and separate CPU-stack/memory diagnostics. Until admission succeeds,
continue offline analysis and deterministic fixture work; do not substitute
GPU-only capture timings or rejected reports for qualified throughput evidence.
Coordinate with [#4428](https://github.com/jakildev/IrredenEngine/pull/4428) on
runner comparison calibration; it does not replace native quiet-host admission.
