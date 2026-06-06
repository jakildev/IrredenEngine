# Plan: detached re-voxelize — round-to-cell occlusion + aliasing (P3)

- **Issue:** #1557
- **Model:** opus
- **Date:** 2026-06-06
- **Epic:** #1553 — see `~/.fleet/plans/issue-1553.md`
- **Blocked by:** #1556

## Scope
Correct coverage + occlusion of the re-voxelized solid at every pose — no holes/specks/dropped cells from round-to-cell, back faces hidden.

## Affected files
- The GPU scatter shader from P2 (aliasing resolution / coverage pass)
- `c_voxel_to_trixel_stage_1.glsl` if exposed-mask handling on re-voxelized cells needs adjustment

## Approach
Per the epic plan §P3. Resolve multi-voxel-per-cell + under-sampled columns on the GPU scatter; reference the attached GRID round-to-cell tradeoff for the acceptable bar.

## Acceptance criteria
No aliasing holes/specks across a full SO(3) spin; occlusion correct; coverage ≥ attached GRID quality.

## Gotchas
No CPU per-voxel loop to patch aliasing — keep it on the GPU scatter.

## Verification
`fleet-run IRCanvasStress --auto-screenshot` across the spin; inspect ROI crops at mid-residual for holes/specks.
