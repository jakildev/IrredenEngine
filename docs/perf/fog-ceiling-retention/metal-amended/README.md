# Fog ceiling retention — native Metal, amended renderer

Five interleaved master/head pairs per lane on the macOS fleet host (macOS
26.5.2, Apple M4 Max, 14 CPUs, AC power, display awake, `macos-debug`,
Debug preset with `-O3`). Master is pinned at
`8a1bc317a11218c27f46cd3c55080635fdb95397`, the PR's merge base, checked out
and built in a scratch worktree; head is `79627c49ce52d2548fde04f36bb32778c84f0fee`,
the amended renderer with the compact split into `c_voxel_visibility_compact`
and `c_voxel_visibility_compact_ceiling` and the per-canvas `fogCeilingEnabled`
lane. The tables in [`../README.md`](../README.md) measure `50c685205`, the
renderer before that amendment; this directory re-measures criterion 8 on the
shipped head.

`../gl-yawed-lanes/run_pairs.py --out <this dir>` ran each pair as head
identity, head treatment, master identity, master treatment through
`scripts/perf/repeat_profile.py --repeats 1 --timeout 120`, with
`IRREDEN_BUILD_DIR` pointing at each arm's own build tree, so no checkout or
build happens between runs. `../gl-yawed-lanes/summarize_pairs.py <this dir>`
means the arms and writes `comparison.json` and the tables below. The common
demo arguments are `--auto-profile 300 --grid-size 64 --yaw 0`; `identity`
adds `--fog-reveal` (eight analytic sources, no ceiling, so the dispatched
compact is the no-ceiling specialization), `treatment` adds
`--fog-reveal --fog-reveal-surface` (every source capped by a soft ceiling, the
canvas treatment on, so the ceiling specialization runs). Every one of the 20
runs is exit 0, `RESULT=CLEAN`, `QUIET=guarded`, GPU-measured, 300 frames,
yaw stationary at 0, zero overflow entries. Each arm's `manifest.json` records
its commit and build tree; the only working-tree change any head manifest
records is `?? docs/perf/fog-ceiling-retention/metal-amended/`, this output
directory itself, written into the worktree as the runs landed.

The grading rule is criterion 8's: a row passes when the head mean is within
the lane's ratio limit of the master mean or within +10 µs of it.
`voxelCompact` is the GPU visibility-compact stage; `voxelStage1` is
voxel-to-trixel stage 1; the named sun-bake measurement is the CPU
`bakeSunShadowMap` row (the profiler emits no GPU row of that name).

## Five-pair means

### identity (limit 1.05× or +10 µs)

| Metric | Master ms | Head ms | Ratio | Delta µs | Pass |
|---|---:|---:|---:|---:|---|
| frame | 20.9340 | 21.1680 | 1.0112× | +234.0 | yes |
| steady frame | 20.8660 | 21.0800 | 1.0103× | +214.0 | yes |
| GPU voxelCompact | 0.3972 | 0.4246 | 1.0690× | +27.4 | **no** |
| GPU voxelStage1 | 0.5722 | 0.5874 | 1.0266× | +15.2 | yes |
| GPU voxelStage2 | 0.4672 | 0.4772 | 1.0214× | +10.0 | yes |
| GPU voxelCardinalElect | 0.4726 | 0.4830 | 1.0220× | +10.4 | yes |
| GPU fogToTrixel | 0.0342 | 0.0340 | 0.9942× | -0.2 | yes |
| GPU voxelSunFaces | 1.0138 | 1.0218 | 1.0079× | +8.0 | yes |
| CPU voxelStage1 | 1.6664 | 1.6960 | 1.0178× | +29.6 | yes |
| CPU fogToTrixel | 0.0082 | 0.0080 | 0.9756× | -0.2 | yes |
| CPU bakeSunShadowMap | 0.0084 | 0.0088 | 1.0476× | +0.4 | yes |

### treatment (limit 1.15× or +10 µs)

| Metric | Master ms | Head ms | Ratio | Delta µs | Pass |
|---|---:|---:|---:|---:|---|
| frame | 21.0800 | 21.0980 | 1.0009× | +18.0 | yes |
| steady frame | 20.9840 | 20.9380 | 0.9978× | -46.0 | yes |
| GPU voxelCompact | 0.4372 | 0.4550 | 1.0407× | +17.8 | yes |
| GPU voxelStage1 | 0.5714 | 0.5856 | 1.0249× | +14.2 | yes |
| GPU voxelStage2 | 0.4678 | 0.4764 | 1.0184× | +8.6 | yes |
| GPU voxelCardinalElect | 0.4728 | 0.4822 | 1.0199× | +9.4 | yes |
| GPU fogToTrixel | 0.0342 | 0.0330 | 0.9649× | -1.2 | yes |
| GPU voxelSunFaces | 1.0150 | 1.0210 | 1.0059× | +6.0 | yes |
| CPU voxelStage1 | 1.7060 | 1.6750 | 0.9818× | -31.0 | yes |
| CPU fogToTrixel | 0.0080 | 0.0080 | 1.0000× | +0.0 | yes |
| CPU bakeSunShadowMap | 0.0088 | 0.0084 | 0.9545× | -0.4 | yes |

Every row passes except identity GPU `voxelCompact`: 0.3972 → 0.4246 ms,
1.069×, +27.4 µs, against a limit of 1.05× or +10 µs.

## The identity compact row

Per-run means of that row, in pair order (ms):

| Arm | 1 | 2 | 3 | 4 | 5 | mean | sd |
|---|---:|---:|---:|---:|---:|---:|---:|
| master | 0.395 | 0.439 | 0.408 | 0.384 | 0.360 | 0.3972 | 0.0293 |
| head | 0.397 | 0.399 | 0.447 | 0.447 | 0.433 | 0.4246 | 0.0250 |

The run-to-run spread of either arm (sd 25 to 29 µs; the profiler's per-frame
min/max for this stage is 0.05 to 0.9 ms) is about three times the rule's
+10 µs tolerance, and the five-pair delta is 1.6 standard errors
(Welch t = 1.59). The treatment lane, whose head arm dispatches the heavier
ceiling specialization, reads 1.041× (+17.8 µs) on the same row.

The dispatched identity kernel is master's. The no-ceiling wrapper compiles
the shared body with `IR_FOG_CEILING_PASS 0`; preprocessing both kernels with
their includes expanded (`clang -E -P` with stub system headers) and diffing
leaves only: inline helpers the kernel never references (the overflow cut-bit
pack/unpack, `fogRevealSurfaceHash01`, the `fogCeiling*` family),
`fogWindowTexel` moved verbatim from a `static` in the kernel file to an
`inline` in `ir_iso_common.metal`, tail fields appended to the
`FogObserverData` constant block that this kernel never reads, the
`fogCeilingEnabled` lane appended to the frame-data block, and two folded
constants (`ceilingPossible = true`, `possibleCut = false`) inside conditions
the kernel already evaluated. `metal_pipeline.cpp` keeps the same threadgroup
size for both compact names, and the identity path selects `VoxelCompactProgram`
exactly as master does (`anyCeilingEnabled` is false for circles added without
a ceiling, whose ceiling lane is `kFogVisionCeilingOff`). No per-frame upload
or encoder was added inside the compact's `GpuSubStageScope`.

### Replication block

A second block of five identity-only pairs, run immediately after the first
with the same binaries (`replication-identity/`, summarized the same way):

| Metric | Master ms | Head ms | Ratio | Delta µs | Pass |
|---|---:|---:|---:|---:|---|
| frame | 21.0760 | 21.0440 | 0.9985× | -32.0 | yes |
| steady frame | 21.0400 | 20.9780 | 0.9971× | -62.0 | yes |
| GPU voxelCompact | 0.4078 | 0.4474 | 1.0971× | +39.6 | **no** |
| GPU voxelStage1 | 0.5720 | 0.5854 | 1.0234× | +13.4 | yes |
| GPU voxelStage2 | 0.4660 | 0.4764 | 1.0223× | +10.4 | yes |
| GPU voxelCardinalElect | 0.4712 | 0.4820 | 1.0229× | +10.8 | yes |
| GPU fogToTrixel | 0.0342 | 0.0332 | 0.9708× | -1.0 | yes |
| GPU voxelSunFaces | 1.0110 | 1.0212 | 1.0101× | +10.2 | yes |
| CPU voxelStage1 | 1.7140 | 1.6980 | 0.9907× | -16.0 | yes |
| CPU fogToTrixel | 0.0082 | 0.0080 | 0.9756× | -0.2 | yes |
| CPU bakeSunShadowMap | 0.0086 | 0.0088 | 1.0233× | +0.2 | yes |

Pooled over the ten pairs the compact row reads master 0.4025 ms (sd 0.0305)
→ head 0.4360 ms (sd 0.0317), 1.083×, +33.5 µs, Welch t = 2.41. The
replication reproduces the direction and size of the first block's delta, so
the row is reported as failing criterion 8's rule as written rather than as a
single noisy block. GPU `voxelStage1` on the same ten pairs reads 1.025×,
+14.3 µs (t = 7.5): a consistent delta inside the ratio limit, on the kernel
that reads the new `fogCeilingEnabled` uniform.

These are native Metal results on one host; they do not establish OpenGL
performance. The native GL arm at yaw 0.35 is in [`../gl-yawed-lanes/`](../gl-yawed-lanes/README.md).
Measured by Claude Fable 5.1 (fleet worker, pool-1).
