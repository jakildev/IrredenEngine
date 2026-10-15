# Fog ceiling retention — native OpenGL, yawed lanes

Three interleaved master/head pairs per lane on the native Windows ship host
(Ryzen 9 5950X, RTX 3060 Ti, NVIDIA 581.80, MSYS2 mingw64, `windows-debug`).
Master is pinned at `8a1bc317a11218c27f46cd3c55080635fdb95397`, the PR's merge
base, built in a scratch worktree; head is the amendment that answers the
Opus recheck (the compact split into `c_voxel_visibility_compact` and
`c_voxel_visibility_compact_ceiling`, and the per-frame `fogCeilingEnabled`
lane). `run_pairs.py` runs every lane once on head, then once on master, per
pair, through `scripts/perf/repeat_profile.py --repeats 1`;
`summarize_pairs.py` means the arms and writes `comparison.json` and the
tables below. Every run is `RESULT=CLEAN`, `QUIET=guarded`, exit 0, with a
fresh report, so every pair is complete and no arm is unmatched. Three pairs
per lane is below the five-pair minimum the corrected criterion 8 rule later
set, so the summarizer marks these lanes insufficient and exits 1 on this
directory; the tables stand as the written rule (i) over three symmetric
pairs, and the criterion 8 table of record is
[`../metal-restored/`](../metal-restored/README.md).

The common demo arguments are `--auto-profile 300 --grid-size 64 --yaw 0.35`:
the camera sits 20.05 degrees off cardinal, so the compact runs in per-axis
split mode and the store, overflow and finalize stages carry the frame. The
`nofog` lane adds nothing, `identity` adds `--fog-reveal` (eight sources, no
ceiling), `treatment` adds `--fog-reveal --fog-reveal-surface`. Every run
reports 749,901 overflow entries on both arms.

The row the recheck measured at 2.05× on Metal, GPU `voxelCompact` on a
rotating frame with the ceiling block present but never executed, reads
0.91× (nofog), 1.02× (identity) and 1.02× (treatment) here, each within one
microsecond of master. The frame rows on this host are CPU-bound (about 60 ms
with a 23 ms `voxelStage1` CPU row on both arms) and are reported for
completeness; the stage limits of criterion 8 are the gate.

## identity (limit 1.05× or +10 µs)

| Metric | Master ms | Head ms | Ratio | Delta µs |
|---|---:|---:|---:|---:|
| frame | 59.1933 | 60.9500 | 1.0297× | +1756.7 |
| steady frame | 58.1833 | 58.8467 | 1.0114× | +663.3 |
| GPU voxelCompact | 0.0400 | 0.0410 | 1.0250× | +1.0 |
| GPU voxelPerAxisStore | 4.3013 | 4.4620 | 1.0374× | +160.7 |
| GPU voxelPerAxisOverflow | 9.3823 | 9.7393 | 1.0381× | +357.0 |
| GPU voxelPerAxisFinalize | 8.5043 | 8.7120 | 1.0244× | +207.7 |
| GPU perAxisScatter | 3.2317 | 3.1907 | 0.9873× | -41.0 |
| GPU fogPerAxis | 0.0097 | 0.0097 | 1.0000× | +0.0 |
| GPU fogOverflow | 0.0327 | 0.0313 | 0.9592× | -1.3 |
| GPU fogToTrixel | 0.0153 | 0.0157 | 1.0217× | +0.3 |
| GPU voxelSunFaces | 0.2680 | 0.2730 | 1.0187× | +5.0 |
| CPU voxelStage1 | 22.9167 | 23.7387 | 1.0359× | +822.0 |
| CPU fogToTrixel | 0.2407 | 0.2287 | 0.9501× | -12.0 |
| CPU bakeSunShadowMap | 0.0423 | 0.0407 | 0.9606× | -1.7 |

## nofog (limit 1.05× or +10 µs)

| Metric | Master ms | Head ms | Ratio | Delta µs |
|---|---:|---:|---:|---:|
| frame | 67.8367 | 66.3133 | 0.9775× | -1523.3 |
| steady frame | 60.8067 | 60.6133 | 0.9968× | -193.3 |
| GPU voxelCompact | 0.0427 | 0.0390 | 0.9141× | -3.7 |
| GPU voxelPerAxisStore | 4.4390 | 4.3647 | 0.9833× | -74.3 |
| GPU voxelPerAxisOverflow | 9.8337 | 9.5783 | 0.9740× | -255.3 |
| GPU voxelPerAxisFinalize | 8.7770 | 8.5963 | 0.9794× | -180.7 |
| GPU perAxisScatter | 3.3357 | 3.2440 | 0.9725× | -91.7 |
| GPU fogPerAxis | 0.0117 | 0.0110 | 0.9429× | -0.7 |
| GPU fogOverflow | 0.0320 | 0.0307 | 0.9583× | -1.3 |
| GPU fogToTrixel | 0.0167 | 0.0173 | 1.0400× | +0.7 |
| GPU voxelSunFaces | 0.2700 | 0.2700 | 1.0000× | +0.0 |
| CPU voxelStage1 | 24.0203 | 23.4280 | 0.9753× | -592.3 |
| CPU fogToTrixel | 0.2447 | 0.2280 | 0.9319× | -16.7 |
| CPU bakeSunShadowMap | 0.0427 | 0.0413 | 0.9688× | -1.3 |

## treatment (limit 1.15× or +10 µs)

| Metric | Master ms | Head ms | Ratio | Delta µs |
|---|---:|---:|---:|---:|
| frame | 58.6567 | 63.6033 | 1.0843× | +4946.7 |
| steady frame | 56.5667 | 62.7667 | 1.1096× | +6200.0 |
| GPU voxelCompact | 0.0403 | 0.0413 | 1.0248× | +1.0 |
| GPU voxelPerAxisStore | 4.3120 | 4.4790 | 1.0387× | +167.0 |
| GPU voxelPerAxisOverflow | 9.4583 | 9.9843 | 1.0556× | +526.0 |
| GPU voxelPerAxisFinalize | 8.5270 | 8.8913 | 1.0427× | +364.3 |
| GPU perAxisScatter | 3.1727 | 3.3200 | 1.0464× | +147.3 |
| GPU fogPerAxis | 0.0107 | 0.0100 | 0.9375× | -0.7 |
| GPU fogOverflow | 0.0300 | 0.0323 | 1.0778× | +2.3 |
| GPU fogToTrixel | 0.0157 | 0.0200 | 1.2766× | +4.3 |
| GPU voxelSunFaces | 0.2640 | 0.2737 | 1.0366× | +9.7 |
| CPU voxelStage1 | 23.0383 | 24.1643 | 1.0489× | +1126.0 |
| CPU fogToTrixel | 0.2353 | 0.2297 | 0.9759× | -5.7 |
| CPU bakeSunShadowMap | 0.0417 | 0.0413 | 0.9920× | -0.3 |

The treatment lane's `fogToTrixel` reads 1.28× on a 16 µs row (+4.3 µs); the
+10 µs allowance covers it, and the same row reads 1.04× on the untreated
lanes.

## field-path (yaw 0, identity, grid on the FIELD selection path)

`field-path/` holds five pairs of the cardinal identity lane
(`--auto-profile 300 --grid-size 64 --yaw 0 --fog-reveal`) with the head's
staged `ir_voxel_face_select.glsl` carrying a scratch mutation that drops the
BODY exemption from `ceilingActive`, so the reveal-governed grid takes the
FIELD path the recheck measured at about 1.3× on stage 1 and stage 2. With
the per-frame flag that path reads one uniform and short-circuits, so the
mutation is output-identical. The master arm is unmodified. The mutation was
reverted and the shaders restaged after the lane; the committed selector
carries the exemption.

See `field-path/README.md` for its table.

These are native OpenGL results on one host; they say nothing about Metal.
Measured by the fleet worker (Claude Fable 5.1, pool-1).
