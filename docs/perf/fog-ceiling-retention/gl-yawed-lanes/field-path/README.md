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
and is excluded by its manifest grade; `summarize_pairs.py` names it. The
means below cover five master and four head runs, every one `RESULT=CLEAN`,
`QUIET=guarded`.

## identity0 (limit 1.05× or +10 µs)

| Metric | Master ms | Head ms | Ratio | Delta µs |
|---|---:|---:|---:|---:|
| frame | 29.0300 | 28.7150 | 0.9891× | -315.0 |
| steady frame | 26.4680 | 26.2550 | 0.9920× | -213.0 |
| GPU voxelCompact | 0.0486 | 0.0430 | 0.8848× | -5.6 |
| GPU voxelStage1 | 2.7564 | 2.7268 | 0.9892× | -29.7 |
| GPU voxelStage2 | 3.0342 | 2.8560 | 0.9413× | -178.2 |
| GPU voxelCardinalElect | 2.6770 | 2.6415 | 0.9867× | -35.5 |
| GPU fogToTrixel | 0.0212 | 0.0238 | 1.1203× | +2.6 |
| GPU voxelSunFaces | 0.4752 | 0.4703 | 0.9896× | -4.9 |
| GPU bakeSunShadowMap | 0.0010 | 0.0010 | 1.0000× | +0.0 |
| CPU voxelStage1 | 8.8398 | 8.7800 | 0.9932× | -59.8 |
| CPU fogToTrixel | 0.0600 | 0.0628 | 1.0458× | +2.8 |
| CPU bakeSunShadowMap | 0.0402 | 0.0410 | 1.0199× | +0.8 |

Stage 1 and stage 2 on the FIELD path read 0.99× and 0.94× of master; the
`fogToTrixel` row is a 21 µs stage within the +10 µs allowance.
