# Plan: profile rotated-frame (continuous-yaw) voxel cost

- **Issue:** #1807 (prerequisite for placeholder epic #1808)
- **Model:** sonnet
- **Date:** 2026-06-13

## Scope

Fill the rotation gap in IRPerfGrid timing. All session profiling was at cardinal
orientation (the "cardinal gather" path); non-cardinal yaw uses the more expensive
"per-axis scatter" path (`creations/demos/perf_grid/main.cpp:133`; the #1739
rotated-frame floor). Measure the delta — it gates the scatter-vs-gather decision
in epic #1808.

## Verified current state

- `--auto-profile` (perf_grid) dumps per-stage CPU + GPU timing and a per-scope
  CPU breakdown; runs at the live camera orientation. Today it's only ever run at
  yaw=0 (cardinal).
- perf_grid takes `--zoom` / `--mode` / `--grid-size` CLI overrides
  (`parseArgs`, ~`:273`); there is **no `--yaw`** yet.
- Non-cardinal residual yaw switches the voxel path: cardinal gather → per-axis
  scatter (`:133-134`), plus a continuous-yaw `rebuildChunkBounds`
  (`buildChunkVisibilityMask(..., useContinuousYaw, visualYaw)` in
  `system_voxel_to_trixel.hpp`).

## Approach (single path)

1. Add a `--yaw <degrees>` CLI override to perf_grid that sets the initial camera
   **residual yaw** (mirror how `--zoom` seeds `setCameraZoom`). Apply it before
   the auto-profile warmup so steady-state timing reflects the rotated path.
2. Run the matrix and capture the `Auto-profile stats` / `Auto-profile GPU` /
   `Auto-profile CPU-scope` lines:
   - modes: `voxel_set`, `dense_set`
   - zoom: 8
   - yaw: 0 (cardinal baseline), 30, 45 (genuine non-cardinal)
3. Post a table: mode × yaw → frame ms / FPS / voxelStage1 GPU, and the
   rotated-vs-cardinal delta per mode. Note whether #1739/#1748 have landed (they
   reduce exactly this cost — the delta must be read against the current master
   state of that path).

## Affected files

- `creations/demos/perf_grid/main.cpp` — `--yaw` parse + initial residual-yaw set.

## Acceptance criteria

- `--yaw` wired in; the residual yaw is genuinely non-cardinal (path actually
  switches to per-axis scatter — confirm via the path, not just visually).
- Numbers captured + posted on #1807 (and rolled into #1808): voxel_set &
  dense_set, zoom8, yaw ∈ {0, 30, 45}.
- The rotated-path cost delta quantified.

## Gotchas

- Make sure the yaw isn't snapped back to a cardinal by camera logic — verify the
  per-axis scatter path is the one being timed.
- Pure measurement task; do not optimize the rotated path here — that's #1739/#1748
  and (if needed) epic #1808. This task only produces the number.
- Keep the change small/throwaway-friendly behind the existing CLI-override pattern.
