# Fog ceiling retention — native Metal, rebased head with the prefix mirror restored

Interleaved master/head pairs per lane on the macOS fleet host (macOS 26.5.2,
Apple M4 Max, 14 CPUs, AC power, display awake: every run log reads
`paced at 120 Hz` and `Discovered 1 display monitors`, `macos-debug`, Debug
preset with `-O3`). Master is pinned at `9d9cecc3f7a490b7735f667b16049d7f3382b07b`,
the PR's merge base after the rebase, checked out and built in a scratch
worktree; head is `2ed29dcf59d88f726e65c2511074c1a1d60a7c02`, the shipped
renderer with the no-ceiling compact declaring master's prefix mirror of
`FogObserverData`. This block is the criterion 8 table of record; the
[`../metal-amended/`](../metal-amended/README.md) block that raised the
identity `voxelCompact` question measured the pre-rebase head with the full
block declared in both specializations.

`../gl-yawed-lanes/run_pairs.py --out <this dir>` ran each pair as head
identity, head treatment, master identity, master treatment through
`scripts/perf/repeat_profile.py --repeats 1 --timeout 120`, with
`IRREDEN_BUILD_DIR` pointing at each arm's own build tree. The common demo
arguments are `--auto-profile 300 --grid-size 64 --yaw 0`; `identity` adds
`--fog-reveal` (eight analytic sources, no ceiling, the no-ceiling
specialization), `treatment` adds `--fog-reveal --fog-reveal-surface` (every
source capped by a soft ceiling, the ceiling specialization).
`../gl-yawed-lanes/summarize_pairs.py <this dir>` means the arms, grades each
row, and writes `comparison.json` (per-run values under `base_runs` /
`head_runs`, per-pair deltas, sd, n and the one-sided 95% lower bound under
`pair_deltas_us`, `sd_us`, `n`, `lower_bound_us`).

Nine pairs were launched. Seven arms returned exit 76 (`QUIET=breach`,
`RESULT=CONTAMINATED`): the `ir-acquire` quiet guard saw another pane's build
uncover the measurement window. Each stays in its directory with its
manifest grade and is excluded by the summarizer, which prints the
exclusions; it was never averaged in. The exclusion is the manifest's,
decided before any acceptance result was read. Every retained run is exit 0,
`RESULT=CLEAN`, `QUIET=guarded`, GPU-measured, 300 frames, yaw stationary at
0, zero overflow entries. The head manifests record a clean source tree at
the measured commit with only this output directory untracked.

## Grading rule

Criterion 8 as corrected by the `## Plan corrections` of 2026-10-14 on #4305:
a row passes when (i) the head mean is within the lane's ratio limit of the
base mean (1.05× identity, 1.15× treatment) or within +10 µs of it; or
(ii) the one-sided 95% lower confidence bound of the paired mean delta,
`mean − t(0.95, n − 1) · sd / √n` over the pairs both arms recorded, is within
that limit, the point ratio is at most 1.15×, and the lane's frame and
steady-frame rows pass (i). **Every row of both lanes passes (i); rule (ii)
is invoked for no row.**

## Means over the retained runs

### identity (limit 1.05× or +10 µs; 8 base / 8 head runs; 7 complete pairs)

| Metric | Master ms | Head ms | Ratio | Delta µs | Pass |
|---|---:|---:|---:|---:|---|
| frame | 19.3888 | 19.4575 | 1.0035× | +68.7 | yes |
| steady frame | 19.4375 | 19.2575 | 0.9907× | -180.0 | yes |
| GPU voxelCompact | 0.3721 | 0.3851 | 1.0349× | +13.0 | yes |
| GPU voxelStage1 | 0.5326 | 0.5449 | 1.0230× | +12.2 | yes |
| GPU voxelStage2 | 0.4551 | 0.4620 | 1.0151× | +6.9 | yes |
| GPU voxelCardinalElect | 0.4606 | 0.4705 | 1.0214× | +9.9 | yes |
| GPU fogToTrixel | 0.0297 | 0.0295 | 0.9916× | -0.3 | yes |
| GPU voxelSunFaces | 0.9974 | 0.9981 | 1.0008× | +0.8 | yes |
| CPU voxelStage1 | 1.4429 | 1.4450 | 1.0015× | +2.1 | yes |
| CPU fogToTrixel | 0.0071 | 0.0073 | 1.0175× | +0.1 | yes |
| CPU bakeSunShadowMap | 0.0075 | 0.0079 | 1.0500× | +0.4 | yes |

### treatment (limit 1.15× or +10 µs; 7 base / 6 head runs; 6 complete pairs)

| Metric | Master ms | Head ms | Ratio | Delta µs | Pass |
|---|---:|---:|---:|---:|---|
| frame | 19.3643 | 19.3733 | 1.0005× | +9.0 | yes |
| steady frame | 19.1557 | 19.4417 | 1.0149× | +286.0 | yes |
| GPU voxelCompact | 0.3881 | 0.3783 | 0.9747× | -9.8 | yes |
| GPU voxelStage1 | 0.5300 | 0.5393 | 1.0176× | +9.3 | yes |
| GPU voxelStage2 | 0.4519 | 0.4563 | 1.0099× | +4.5 | yes |
| GPU voxelCardinalElect | 0.4570 | 0.4618 | 1.0106× | +4.8 | yes |
| GPU fogToTrixel | 0.0293 | 0.0290 | 0.9902× | -0.3 | yes |
| GPU voxelSunFaces | 0.9933 | 0.9838 | 0.9905× | -9.5 | yes |
| CPU voxelStage1 | 1.4683 | 1.4630 | 0.9964× | -5.3 | yes |
| CPU fogToTrixel | 0.0070 | 0.0072 | 1.0238× | +0.2 | yes |
| CPU bakeSunShadowMap | 0.0077 | 0.0080 | 1.0370× | +0.3 | yes |

## The identity compact row

The row the previous block failed reads 0.3721 → 0.3851 ms,
1.035×, +13.0 µs, inside the 1.05× half of the written rule.
Paired by interleaving order over the 7 complete pairs the deltas are
+16, +13, +27, +61, -11, -15, +37 µs (paired mean +18.3 µs, sd 26.6 µs, SE 10.1 µs), lower bound
-1.3 µs against a limit of 18.6 µs. The reading is
consistent with zero: the row's paired spread on this host is 25 to 60 µs
between runs, so this block cannot separate "the declared block was the
mechanism" from "the previous block's +27 / +40 µs was the row's noise". It
records only that the shipped head's no-ceiling compact, declaring master's
prefix mirror, is within the written identity limit against the merge base.
The treatment lane, whose head arm dispatches the ceiling specialization with
the full block, reads the same row at 0.975× (-9.8 µs).

The GPU rows on the identity path that take `selectVoxelFace`'s new
`fogCeilingEnabled` read and the 272 → 288 byte frame-data block
(`voxelStage1`, `voxelStage2`, `voxelCardinalElect`) move +7 to +12 µs
(1.5 to 2.3 %), the priced cost of the one-uniform design, inside the limit;
`voxelStage1` paired reads +23, +2, +15, +39, +30, +15, +16 µs
(sd 12.0).

These are native Metal results on one host; they do not establish OpenGL
performance. The native GL arm at yaw 0.35 is in
[`../gl-yawed-lanes/`](../gl-yawed-lanes/README.md). Measured by Claude
Fable 5.1 (fleet worker, pool-3).
