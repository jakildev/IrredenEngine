# Plan: render — rotated-solidity harness (isolate cardinals + zoom tier + near-cardinal sampling)

- **Issue:** #1882 (**RE-SCOPED** — was "cardinal-gather coverage loss")
- **Model:** opus
- **Date:** 2026-06-17
- **Epic:** #1881 — see .fleet/plans/issue-1881.md for full context
- **Gated on:** PR #1880 (harness) — merged. **Unblocks #1883.**

## Re-scope rationale
The #1885 investigation proved the original premise (single-canvas cardinal gather
broken) was a **misdiagnosis**: held statically at each exact cardinal the
single-canvas path renders coverage 1.0 / 0 holes, and an early-return probe in the
cardinal store showed the ramp's broken "cardinal" rows are rendered by the
**per-axis** path (90°/180° unchanged when the cardinal store early-returns; only
270° vanishes). The ramp's coverage loss is the per-axis path (→ #1883), captured at
near-cardinal *residual* frames because per-axis is still allocated (allocation /
deadband lag, with a yaw-sign asymmetry). So #1882 becomes the harness-methodology
fix the misdiagnosis exposed — the harness must distinguish render paths and surface
the artifacts that zoom 0.8 hides.

## Scope
1. **Isolate the single-canvas path at the cardinals.** Settle/hold at each exact
   cardinal so per-axis is released before capture; fix the 270°-vs-90°/180°
   asymmetry (per-axis-release deadband / `computeYawSplit` residual sign at exact
   cardinals). Settled cardinals must read clean (>= 0.99).
2. **Add a zoomed tier on a small cube.** High zoom on a small cube (small
   `--grid-size` + high `--zoom`, or drive canvas_stress) so the fine per-axis
   face-alignment / seam / sliver artifacts are visible; zoom 0.8 on the 64^3 cube
   under-resolves them.
3. **Finer near-cardinal sampling.** Artifacts peak **approaching** 180°/270° (~±1–10°
   residual), not at exact cardinals or the 45° flips. Sample densely there.
4. **Report the render path** (single-canvas vs per-axis) per pose so a "cardinal"
   row is unambiguous.

## Affected files
- `scripts/dev/perf-grid-rotate-sweep` — sampling set + settle + per-pose path label + zoom tier
- `creations/demos/perf_grid/main.cpp` — `kYawRampShots`: near-cardinal + zoom-tier shots; expose per-axis-active state
- (read) `engine/prefabs/irreden/render/per_axis_canvas.hpp` + `system_voxel_to_trixel` `beginTick` `syncAllocationToCameraYaw` (allocation/deadband lifecycle + the 270° asymmetry); `engine/prefabs/irreden/render/camera.hpp` `computeYawSplit` (residual sign)

## Acceptance criteria
- Per-pose render-path label; **settled exact cardinals read clean (>= 0.99) on BOTH backends.**
- The zoomed near-cardinal tier surfaces the per-axis face-alignment/seam artifacts
  as a measurable signal (coverage / silhouette raggedness) + ROI crops.

## Gotchas
- The per-axis-release deadband + `computeYawSplit` residual sign at exact cardinals
  is the 270°-vs-90°/180° asymmetry; nudge the captured yaw within the deadband (or
  hold long enough for the free-at-cardinal gate to fire).
- **Do NOT change the cardinal store / single-canvas gather — it is verified correct.**

## Verification
macOS: `bash scripts/dev/perf-grid-rotate-sweep` with the new settle + zoom +
near-cardinal sampling; confirm cardinals clean + the zoom tier surfaces the seams.
Linux: same via fleet-build/run. Cross-host IRShapeDebug smoke.
