# GPU profiling — per-pass timing + cull diagnostics

Both diagnostics gate on `gpuStageTiming().enabled_`; shipping builds pay
nothing. Keep it off except during a matrix run.

## Per-pass timing

Each render stage brackets its GPU work with an async timestamp query
(`GL_TIMESTAMP` / `MTLCounterSample`); results land in `GpuStageTiming` (one
`float ...Ms_` per stage, `engine/prefabs/irreden/render/gpu_stage_timing.hpp`).
Stages: canvasClear, voxelCompact, voxelStage1, voxelStage2, shapeCompact,
shapePass0, shapePass1, textToTrixel, buildLightOcclusionGrid, computeVoxelAO,
bakeSunShadowMap, computeSunShadow, computeLightVolume, lightingToTrixel,
fogToTrixel, trixelToTrixel, trixelToFb, entityCanvasToFb,
resolvePerAxisScreenDepth, fbToScreen — each with a soft budget share
(`GpuStageInfo::budgetShare_`) surfaced as `overBudget`.

```lua
ir.render.setGpuTimingEnabled(true)
for _, row in ipairs(ir.render.getPassTimings()) do
    print(row.name, row.ms, row.budgetMs, row.overBudget)
end
```

C++: `IRRender::gpuStageTiming().*Ms_` and `IRRender::gpuStageRegistry()`.
The shutdown `save_files/profile_report.txt` carries the same data under
`--- GPU stage timing ---`; the matrix aggregates it across cells.

Quote per-stage GPU timings in a PR or acceptance-evidence body from the
`profile_report.txt` average (`--auto-profile N`; better, a multi-run mean with
spread via `compare_perf_runs.py`), never a single-frame HUD /
`gpuStageTiming().*Ms_` / `lastFrameMs` read — one frame can be ~2× the
steady-state average, especially on the per-axis path.

## Voxel cull effectiveness

`VOXEL_TO_TRIXEL_STAGE_1` reads the prior frame's
`IndirectDispatchParams.visibleCount` via `Buffer::getSubData` before zeroing
the buffer — sync-free because frame N+1 reads frame N's committed value.

- `gpuStageTiming().visibleVoxelCount_` / `totalVoxelCount_` — last frame.
- `voxelCullAccumulator()` — running sum / max / samples over the window
  (reset on `enableFrameTiming(true)`).
- Lua `ir.render.getVoxelCullStats()` → `{visible, total, samples,
  avgVisible, avgTotal, maxVisible, maxTotal}` (plus the feeder fields in
  `docs/perf/README.md` §"Voxel cull stats").
- Profile report `--- Voxel cull stats ---` (avg / max / ratio);
  `compare_perf_runs.py` renders the `voxel cull effectiveness` table.

The ratio shrinks roughly as `1/zoom²` when culling works; a flat ratio across
zooms is the signature of an ineffective viewport cull
([`common_bottlenecks.md`](common_bottlenecks.md) "Shadow-feeder sweep
inflates cull bounds at high zoom").

## When per-pass timing is not enough

- A single-frame anomaly hidden in the average → RenderDoc / Xcode GPU
  capture by the human, pointed at the pass the comparator's GPU table names.
- Unclear split inside a shader → a debug uniform incremented per work-group,
  read back via SSBO.
- Suspected dispatch grid → log `gl_NumWorkGroups.{x,y,z}` from the shader
  once per frame.

## Recurring GPU hotspots

- Wrong dispatch grid → `voxelStage1` / `shapePass1` 5–20× budget; use
  `voxelDispatchGridForCount()`.
- Workgroup size mismatched with the dispatch math → wrong output, often only
  visible as a visual regression.
- `subdivisions²` Z dimension growing with zoom — by design, but it amplifies
  any per-invocation cost.
- Per-frame SSBO upload that should be push-at-mutation
  (`.claude/rules/cpp-ecs.md` §"No dirty flags on components").
