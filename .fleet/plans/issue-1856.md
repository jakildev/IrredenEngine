# Plan: fix rotated-frame profiling instrumentation

- **Issue:** #1856 (follow-up to #1807; unblocks reliable rotation data for epic #1808)
- **Model:** opus
- **Date:** 2026-06-14

## Scope

Make per-axis-scatter (rotated-frame) profiling trustworthy: fix the `CULL VOX`
overlay counter, correctly attribute the per-axis scatter GPU cost, and label
settled-vs-transient timing. Rendering is already correct — this is measurement
correctness only.

## Verified current state (architect verification, 2026-06-14)

Captured on `dense_set` (262144 voxels), macOS/Metal, post-merge master:

- **Rendering correct** — complete solid cube at zoom1/4/8 × {cardinal, rotated},
  no dropped voxels (visual A/B from `--auto-screenshot` shot pairs).
- **`CULL VOX` counter inconsistent** (`system_perf_stats_overlay.hpp` reading
  `IRRender::gpuStageTiming()` / the voxel cull stats): zoom1 cardinal
  `262144/262144`; zoom1 rotated `0/262144`; zoom8 cardinal `0/262144` (cube
  fills screen — wrong); zoom8 rotated `214208/262144`. The cardinal-path
  counter drops to 0 when the per-axis path / a zoom-dependent branch is active.
- **Per-axis GPU timing under-attributed** — `--auto-profile` zoom8 rotated
  `voxelStage1` GPU = 4.6ms while the path processes ~214k voxels (a less-settled
  read showed ~25ms). The per-axis scatter dispatch is likely outside the
  `voxelStage1` GPU timer scope (the cardinal analogue was #1778).
- **Settled vs transient** — `--auto-profile` (90 frames) vs `--auto-screenshot`
  (`settleFrames_=3`, perf_grid `createAutoScreenshotSystem`) disagree ~3.4× on
  zoom8 rotated frame time (17.7 vs 60.9ms): the screenshot captures an unsettled
  camera-cut frame (per-axis chunk-bounds rebuild).

Relevant files: `engine/prefabs/irreden/render/gpu_stage_timing.hpp` /
`gpu_stage_timing_observer.hpp` (timer scopes), the per-axis scatter dispatch in
`engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` (the
`vs1_per_axis` / `PerAxisScatterProgram` path), and
`engine/prefabs/irreden/render/systems/system_perf_stats_overlay.hpp` (the
`CULL VOX` readout).

## Approach (single path)

1. **Counter.** Trace the `CULL VOX` numerator back to its source (cardinal-gather
   compacted count vs per-axis survivor counts). Make it sum the per-axis path's
   processed voxels when that path is active, and confirm it's nonzero at zoom8
   cardinal. One consistent "visible/total" semantic across both paths and all
   zooms.
2. **GPU timing.** Bring the per-axis scatter dispatch inside the `voxelStage1`
   GPU timer scope (preferred — keeps one comparable stage number) OR add a
   dedicated `VOX-PERAXIS` stage to `GpuStageTiming` + the overlay. Verify the
   stage sums reconcile with the wall-clock frame time at steady state for both
   cardinal and rotated.
3. **Settled-vs-transient.** Document that auto-screenshot per-shot HUD timing is
   transient (not a perf source); the settled `--auto-profile` is authoritative.
   Optionally raise `settleFrames_` for perf-relevant shots or mark the overlay
   timing "unsettled" during shot cycling.

## Affected files

- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` — per-axis dispatch timer scope + cull-count accounting
- `engine/prefabs/irreden/render/gpu_stage_timing.hpp` / `gpu_stage_timing_observer.hpp` — stage field(s) if a `VOX-PERAXIS` stage is added
- `engine/prefabs/irreden/render/systems/system_perf_stats_overlay.hpp` — `CULL VOX` readout + any new stage row
- `creations/demos/perf_grid/main.cpp` — settle-frames / timing-source note (if touched)

## Acceptance criteria

- `CULL VOX` reads a sensible nonzero count at zoom8 cardinal and is consistent
  across cardinal/per-axis paths and zoom (spot-check the four cases above).
- Per-axis scatter GPU cost is attributed so the reported stage sums reconcile
  with total frame time at steady state (auto-profile cardinal AND rotated).
- A one-line settled-vs-transient note lands (overlay help and/or docs).
- Builds clean both hosts; no behavior change to rendering output.

## Gotchas

- Pure instrumentation — must NOT change rendered output. Verify a render-verify /
  screenshot A/B is unchanged after the fix.
- The per-axis scatter path is the rotated/continuous-yaw branch (`vs1_per_axis`);
  don't conflate it with the cardinal gather counters.
- Re-run the #1807 matrix after this lands to replace the provisional rotation
  numbers in #1808 with trustworthy ones.
