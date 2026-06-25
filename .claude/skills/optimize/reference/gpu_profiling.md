# GPU profiling — per-pass timing + cull diagnostics

The engine has two GPU diagnostics wired into the render pipeline. Both
gated on `gpuStageTiming().enabled_`.

## Per-pass GPU timing

Every major render stage brackets its GPU work with a timer; the
result lands in `GpuStageTiming` (one `float ...Ms_` per stage in
`engine/prefabs/irreden/render/gpu_stage_timing.hpp`).

Registered stages (canvasClear, voxelCompact, voxelStage1, voxelStage2,
shapeCompact, shapePass0, shapePass1, textToTrixel,
buildLightOcclusionGrid, computeVoxelAO, bakeSunShadowMap,
computeSunShadow, computeLightVolume, lightingToTrixel, fogToTrixel,
trixelToTrixel, trixelToFb, entityCanvasToFb, resolvePerAxisScreenDepth,
fbToScreen) each carry a soft budget share (`GpuStageInfo::budgetShare_`).
A pass exceeding its share is flagged via `overBudget` in the Lua surface.

Lua surface:

```lua
ir.render.setGpuTimingEnabled(true)
-- ... let the scene render ...
for _, row in ipairs(ir.render.getPassTimings()) do
    print(row.name, row.ms, row.budgetMs, row.overBudget)
end
```

C++ surface: `IRRender::gpuStageTiming().*Ms_` fields and the
`IRRender::gpuStageRegistry()` table.

The shutdown profile report (`save_files/profile_report.txt`) includes
the same data under `--- GPU stage timing ---`. The matrix script
aggregates this across cells.

## Voxel cull effectiveness

`VOXEL_TO_TRIXEL_STAGE_1` reads the prior frame's
`IndirectDispatchParams.visibleCount` via `Buffer::getSubData` before
zeroing the buffer — sync-free because frame N+1 reads frame N's
already-committed value. Added in PR #1019.

Surfaces:

- `gpuStageTiming().visibleVoxelCount_` / `totalVoxelCount_` — last frame.
- `voxelCullAccumulator()` — running sum / max / sample count across
  the measurement window (reset on `enableFrameTiming(true)`).
- Lua: `ir.render.getVoxelCullStats()` → `{visible, total, samples,
  avgVisible, avgTotal, maxVisible, maxTotal}`.
- Profile report: `--- Voxel cull stats ---` section with avg / max /
  ratio.

The matrix script parses this and `compare_perf_runs.py` renders a
`voxel cull effectiveness` table. Ratio shrinks roughly as `1/zoom²` if
culling is working; a flat ratio across zooms is the signature of
ineffective viewport culling (see [`common_bottlenecks.md`](common_bottlenecks.md)
"Shadow-feeder sweep inflates cull bounds at high zoom").

## Backend caveat: sync vs async

The current per-pass timer brackets are `glFinish()`-style (i.e. they
stall the GPU at each boundary so the CPU clock sample is meaningful).
Accurate but adds throughput cost — default is **off**, flip on only
during a matrix run.

Async path (`GL_TIMESTAMP` query objects, `MTLCounterSampleBuffer` for
Metal) is tracked in issue #1021. Once that lands, per-pass timing can
be on by default without measurable cost.

The cull-stats `getSubData` readback is also synchronous but only fires
when `gpuStageTiming().enabled_` is true. Shipping builds pay zero
cost.

## When per-pass timing isn't enough

- Single-frame anomaly hidden in the average → RenderDoc / Xcode GPU
  capture. The agent can't drive a GUI; hand off to the human with the
  specific pass name to focus on (use the `compare_perf_runs.py` GPU
  table to identify it).
- Shader-internal cost split unclear → temporarily add a debug uniform
  incremented per work-group, read back via SSBO. Coarse but works.
- Compute dispatch grid suspected wrong → log
  `gl_NumWorkGroups.{x,y,z}` from inside the shader once per frame.

## Common GPU hotspots

See [`common_bottlenecks.md`](common_bottlenecks.md). Recurring patterns
that show up in `getPassTimings()`:

- Wrong dispatch grid → `voxelStage1` or `shapePass1` 5–20× their
  budget. Fix: use `voxelDispatchGridForCount()`.
- Workgroup-size mismatch with dispatch math → stage runs but produces
  wrong output (often invisible until visual regression hits).
- `subdivisions²` Z-dimension growing with zoom — by design, but
  amplifies any per-invocation cost.
- Per-frame SSBO upload that could be push-at-mutation — kills GPU
  throughput at scale. Fix per `cpp-ecs.md` "No dirty flags on
  components".
