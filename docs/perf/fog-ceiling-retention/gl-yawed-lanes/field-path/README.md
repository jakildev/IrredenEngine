# field-path — cardinal identity lane with the grid on the FIELD selection path

Same host, builds and tooling as the parent directory. Five interleaved pairs
of `--auto-profile 300 --grid-size 64 --yaw 0 --fog-reveal` (eight sources,
no ceiling, cardinal camera, so the single-list compact and the stage-1 /
stage-2 / cardinal-election kernels carry the frame).

The head arm's staged `ir_voxel_face_select.glsl` carried a scratch mutation
that removed the BODY exemption from `ceilingActive`, so the reveal-governed
grid (BODY-tagged by `IRPerfGrid --fog-reveal`) took the FIELD path the Opus
recheck measured at about 1.3× on stage 1 and stage 2 before this amendment.
With the per-frame `fogCeilingEnabled` lane that path short-circuits on one
uniform read, so the mutation is output-identical; the committed selector
keeps the exemption and the shaders were restaged after the lane. The master
arm is the unmodified pinned build.

`head-identity0-1` returned `RESULT=CONTAMINATED` (`QUIET=breach`, exit 76)
and is excluded by its manifest grade; `summarize_pairs.py` names it, and its
clean partner `base-identity0-1` is unmatched and enters no statistic. The
means below cover the four complete pairs (2 to 5), every arm `RESULT=CLEAN`,
`QUIET=guarded`. Four pairs is below the five the corrected criterion 8 rule
requires, so the summarizer reports the lane as insufficient and exits 1 on
this directory: the lane is the recheck's FIELD-path diagnostic, not a
criterion 8 table, and its rows are informational.

## identity0 (limit 1.05× or +10 µs; 4 complete pairs, informational)

| Metric | Master ms | Head ms | Ratio | Delta µs |
|---|---:|---:|---:|---:|
| frame | 29.0675 | 28.7150 | 0.9879× | -352.5 |
| steady frame | 26.5950 | 26.2550 | 0.9872× | -340.0 |
| GPU voxelCompact | 0.0490 | 0.0430 | 0.8776× | -6.0 |
| GPU voxelStage1 | 2.7058 | 2.7268 | 1.0078× | +21.0 |
| GPU voxelStage2 | 2.9820 | 2.8560 | 0.9577× | -126.0 |
| GPU voxelCardinalElect | 2.6233 | 2.6415 | 1.0070× | +18.2 |
| GPU fogToTrixel | 0.0208 | 0.0238 | 1.1446× | +3.0 |
| GPU voxelSunFaces | 0.4597 | 0.4703 | 1.0228× | +10.5 |
| GPU bakeSunShadowMap | 0.0010 | 0.0010 | 1.0000× | +0.0 |
| CPU voxelStage1 | 8.7615 | 8.7800 | 1.0021× | +18.5 |
| CPU fogToTrixel | 0.0605 | 0.0628 | 1.0372× | +2.3 |
| CPU bakeSunShadowMap | 0.0405 | 0.0410 | 1.0123× | +0.5 |

Stage 1 and stage 2 on the FIELD path read 1.01× and 0.96× of master; the
`fogToTrixel` row is a 21 µs stage within the +10 µs allowance.
