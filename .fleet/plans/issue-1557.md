# Plan: detached re-voxelize — round-to-cell occlusion + aliasing (P3)

- **Issue:** #1557
- **Model:** opus
- **Date:** 2026-06-06
- **Epic:** #1553 — see `.fleet/plans/issue-1553.md`
- **Blocked by:** #1556

## Scope
Make the re-voxelized solid correct at every pose. P1 ships **two distinct defects** — fix both:
1. **Coverage gaps** — round-to-cell holes (the rotated lattice maps non-bijectively; some destination cells get no source voxel → see-through holes).
2. **Wrong-shaped / "flag" trixels** — faces emitting with the wrong set/orientation on the rotated cells (NOT a coverage problem; denser fill won't remove them).

## Defect 2 root cause (the "flag" trixels) — confirm, then fix
Observed (PR #1561 screenshots): the detached re-voxelize solids show angular "flag/pennant" trixels distinct from the holes. **Likely cause** (confirm with GPU/headless instrumentation — leads, not a mandate; the architect has been wrong on this surface before):
- The **exposed-face mask is computed on model-space (un-rotated) adjacency** (`recomputeFaceOccupancy` on the authored voxels in the demo) but `SYSTEM_REBUILD_DETACHED_VOXELS` rewrites **only positions** (`globals[i].pos_`), never the mask. After re-voxelizing, the mask is **stale vs. the rotated cell grid**: a face that became interior in the rotated layout still passes the `exposed` test and emits (spurious back-face → flag), and a face newly exposed by the rotation is suppressed (extra hole).
- Secondary lead if the above doesn't fully account for it: the re-voxelize cardinal `faceDeform_` / face-orientation for cells whose rotated neighbors changed.

**Fix direction:** make face-emit correct on the **re-voxelized (destination) cell adjacency**, not the model-space mask — recompute the exposed mask against the rotated cells each frame (cheap on the small detached pool), or resolve exposure via GPU occlusion on the rotated grid rather than the precomputed mask. This is the same place the GRID attached path resolves it (it re-probes neighbors on the re-voxelized world cells).

## Affected files
- The P2 GPU scatter / fill path (where the rotated cells are produced) — add re-voxelized-adjacency exposure
- `system_rebuild_detached_voxels.hpp` / `component_voxel_pool.hpp` — exposed-mask recompute against rotated cells (if CPU-side), or
- `c_voxel_to_trixel_stage_1.glsl` / the compact pass — if exposure resolves on the GPU
- `c_voxel_to_trixel_stage_1.glsl` — coverage (defect 1)

## Approach
Per the epic plan §P3, plus defect 2 above. For defect 1: resolve multi-source-per-cell (atomicMin keep-nearest) and fill under-sampled columns so coverage ≥ the attached-GRID round-to-cell bar. For defect 2: re-derive the exposed set on the rotated cells.

## Acceptance criteria
- No coverage holes/specks across a full SO(3) spin of the asymmetric solid (defect 1).
- **No wrong-shaped / flag trixels** — the emitted face set + orientation is correct on the rotated cell grid; no spurious interior/back faces, no suppressed exposed faces (defect 2).
- Occlusion correct (back faces hidden); coverage + face-emit quality ≥ the attached GRID re-voxelize path.

## Gotchas
- The two defects have **different causes** — fixing coverage (denser fill) does NOT remove the flag trixels (stale exposed set). Verify both independently.
- Don't reintroduce a CPU per-voxel loop on the hot path for either fix — keep coverage on the GPU scatter; if the exposed-mask recompute is CPU-side, it runs in the (already CPU) `SYSTEM_REBUILD_DETACHED_VOXELS`, bounded to the small detached pool.

## Verification
`fleet-run IRCanvasStress --auto-screenshot` across the spin; ROI crops at mid-residual: confirm (a) no holes AND (b) no flag trixels — the solid surface reads clean, faces correctly oriented. Compare against the attached GRID solid (clean reference) in the same scene.
