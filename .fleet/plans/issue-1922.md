# Plan: render: jitter-free rotation validation harness — all canvas types × SDF shapes × angles

- **Issue:** #1922
- **Date:** 2026-06-19

**Model:** opus
**Part of:** epic #1881 (rotated-voxel correctness under camera Z-yaw)
**Blocked by:** (none) — extends the epic's existing harness; foundational (the acceptance gate for the other jitter children).
**Related:** PR #1880 (rotated-solidity substrate this extends); #1882 (rotated-solidity harness).

## Why (motivation)
The epic's `scripts/dev/perf-grid-rotate-sweep` harness (PR #1880) measures per-frame coverage/silhouette on a single 64³ cube — it catches solidity/coverage loss but **not temporal jitter**, and it doesn't cover the SDF shapes or the multiple canvas/render paths. To validate "no jitter" (the stated completion bar) we need a harness that sweeps **all canvas types × all SDF shapes × camera angles** and flags frame-to-frame instability.

## Scope
Extend the rotated-solidity substrate into a temporal-jitter validation matrix:
- **Render/canvas paths:** single-canvas gather (cardinal GRID), per-axis scatter (residual / smooth yaw), detached / world-placed entity canvas (SO(3)), SDF analytical, voxel-pool raster.
- **Shapes:** all 8 SDF types × {SDF-rendered, voxel-pool-rendered}.
- **Angles:** 4 cardinals + 4 inter-cardinals + a fine continuous sweep (≤1° steps over 360°), at zooms {1,4,8,16}.
- **Metric:** a **temporal** comparator — consecutive fine-sweep frames differ only by expected smooth sub-pixel motion (no pixel toggling, no crawling bands). Build on `scripts/render-silhouette-metric.py` / `render-verify.py` / `render_metric_util.py`.

## Approach
- Drive the sweep via `shape_debug --spin-yaw --auto-screenshot N` (already exists) and/or extend `perf_grid --yaw-ramp`; add per-shape ROI crops.
- Add `scripts/render-jitter-metric.py` (temporal frame-delta) + `scripts/dev/shape-rotate-jitter-sweep` mirroring `perf-grid-rotate-sweep`.
- Establish a baseline table (like the epic's) on origin/master for both backends; define the pass threshold (the crux: separating smooth sub-pixel motion from true jitter).

## Acceptance criteria
- One command produces a per-(shape × canvas × angle) jitter score on **both** Metal and OpenGL, with documented pass thresholds.
- Reproduces the known Bug-A artifact (curved-SDF band crawl) as a **FAIL on origin/master** and **passes** once the SDF-jitter child lands.
- Committed reference baseline + a metric unittest (mirroring `scripts/tests/test_render_silhouette_metric.py`).

## Files (start)
`scripts/render-silhouette-metric.py`, `scripts/render_metric_util.py`, `scripts/render-verify.py`; new `scripts/render-jitter-metric.py` + `scripts/dev/shape-rotate-jitter-sweep`; `creations/demos/shape_debug/main.cpp` (--spin-yaw shot table, ROI crops); `scripts/tests/` (metric unittest).

## Gotchas
- Lighting must be converged frame-to-frame (the epic's harness already ensures this) so jitter reflects geometry, not a lighting settle.
- The hard part is the threshold — distinguishing expected smooth sub-pixel motion from true jitter.

## References
Epic #1881; PR #1880 (substrate); #1882 (rotated-solidity); existing `scripts/render-*-metric.py`.
