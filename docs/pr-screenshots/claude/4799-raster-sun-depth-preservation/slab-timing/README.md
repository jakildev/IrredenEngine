# Native three-slot depth slabs: guarded current-tree cost

The sparse depth-slab prototype lives on branch
`claude/4799-sun-raster-depth-slabs` at
[`ece131393`](https://github.com/jakildev/IrredenEngine/tree/ece131393858f732df072d2aea6deed72b18eea0/docs/pr-screenshots/claude/4799-sun-raster-depth-slabs)
(its README holds the 63 graded captures, the harness and the staged
ablation). Its own five-pair timing was taken on builds of master `a6b9953c3`
outside a benchmark window. This directory re-measures that cost on the
current tree under `ir-acquire benchmark`.

## Arms

Both arms build `ece131393`, whose merge-base with master is `d72f88820`, the
master this PR merges. They differ only in `kSunDepthSlabSlots` in the three
layout twins (`sun_face_query_layout.hpp`, `ir_sun_face_query_layout.glsl`,
`ir_sun_face_query_layout.metal`):

| Arm | Slots | IRCanvasStress sha256 | IRPerfGrid sha256 |
|---|---|---|---|
| baseline | 0 (the branch's shipped default: master's buffer layout, receiver hashes equal to master's on the four `captures-default.json` cases of that branch) | `381086ce1b3f…` | `533493254d62…` |
| head | 3 | `5a9403eedc3b…` | `fc7bbb5795c2…` |

The head tree was built with the slot edit, cloned (`cp -Rc`), and the
original tree rebuilt after the edit was reverted. Each tree's staged
`shaders/metal/ir_sun_face_query_layout.metal` carries its own slot count. The
manifests' `head` and `changes` fields describe the source tree at run time
(`ece131393`, clean); the arms are identified by `binary_sha256` and
`shader_sha256`, constant across all 50 runs. Apple M4 Max, macOS Metal,
Debug, AC power.

## Guarded windows

One `ir-acquire --timeout 3600 benchmark` window per cell, five interleaved
pairs each, through the branch's `ab_timing.py` (`repeat_profile.py`, 360
profiled frames for IRCanvasStress and 60 for IRPerfGrid). A window that
ended in `QUIET-BREACH` (exit 76: another pane's dispatch lease never parked)
was discarded whole and retaken; only clean windows are retained. Every
retained run is `RESULT=CLEAN` with a fresh report.

| Cell | Windows discarded before the retained one |
|---|---|
| `canvas-lower` | 0 |
| `canvas-stacked-128` | 1 |
| `canvas-default-raster` | 4 |
| `perf-grid-zoom1` | 4 |
| `perf-grid-zoom4` | 1 |

## Result

Head over baseline, mean of five pairs (per-arm min..max in each cell's
`ab-timing.md`; samples in `ab-timing.json`; per-run `manifest.json` and
`summary.md` under `pair-*/`):

| Cell | Frame mean | Frame p99 | GPU envelope | GPU `bakeSunShadowMap` |
|---|---|---|---|---|
| IRCanvasStress raster route, lower caster | 1.452x | 1.614x | 1.560x | 1.979x |
| IRCanvasStress raster route, 128 distant casters | 1.628x | 1.205x | 1.681x | 2.132x |
| IRCanvasStress default scene, raster route | 1.012x | 0.982x | 1.030x | 1.389x |
| IRPerfGrid zoom 1 | 0.970x | 1.051x | 1.046x | 0.993x |
| IRPerfGrid zoom 4 | 1.009x | 0.994x | 0.984x | 0.930x |

The verdict of the older measurement holds on the current tree: both small
raster-route cells exceed the 1.10x gate on frame mean, p99 and GPU envelope,
and `bakeSunShadowMap` roughly doubles. The default raster scene and both
IRPerfGrid cells stay inside the frame gate, although the default scene's
bake still rises 1.39x, outside both arms' spread. Individual ratios move
between the two runs (lower-caster mean 1.285x there, 1.452x here; 128-caster
p99 2.220x there, 1.205x here; envelopes 1.601x and 1.846x there, 1.560x and
1.681x here), but every raster-cell frame-mean and envelope ratio is above the
gate in both, so the rejection does not depend on which run is read.

## Replay

```
git checkout --detach ece131393
# set kSunDepthSlabSlots = 3u in the three layout twins
fleet-build --target IRCanvasStress IRPerfGrid
cp -Rc build <head-tree>
git checkout -- engine/prefabs/irreden/render/sun_face_query_layout.hpp \
  engine/render/src/shaders/ir_sun_face_query_layout.glsl \
  engine/render/src/shaders/metal/ir_sun_face_query_layout.metal
fleet-build --target IRCanvasStress IRPerfGrid
ir-acquire --timeout 3600 benchmark -- python3 \
  docs/pr-screenshots/claude/4799-sun-raster-depth-slabs/ab_timing.py \
  --output <dir>/<cell> --baseline build --head <head-tree> --pairs 5 --cells <cell>
```

Retake a window whose `ir-acquire` exit is 76.
