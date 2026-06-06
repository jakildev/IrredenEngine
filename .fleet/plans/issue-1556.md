# Plan: detached re-voxelize — GPU scatter fill (P2)

- **Issue:** #1556
- **Model:** opus
- **Date:** 2026-06-06
- **Epic:** #1553 — see `.fleet/plans/issue-1553.md`
- **Blocked by:** #1555

## Scope
Move the per-frame private-pool cell-fill from CPU (P1) to the GPU, so a rotating detached entity costs O(entities) per-frame upload (one quat) + a GPU-parallel fill, not O(authored voxels) CPU re-rasterize.

## Resource model — DECIDED (architect, design-block resolution on PR #1562)

The plan's original "reuse #1396's binding-17/18 buffers" is **superseded** — those bindings are single-instance/shared, and the demo has **two** `DETACHED_REVOXELIZE` canvases, so a shared local buffer would re-seed per-canvas per-frame (O(authored voxels), and clobbers one solid). Use a **per-pool resident GPU locals buffer** instead:

- Each `DETACHED_REVOXELIZE` pool **owns a resident SSBO** of its authored locals (+ per-voxel offsets) — GPU-RAII on `C_VoxelPool`, same pattern as `C_TriangleCanvasTextures`. **Seeded once** at pool allocation; re-seeded **only on pool mutation** (voxel add/remove), never per frame.
- Per frame, the only upload is the **canvas rotation quat** — one per re-voxelize canvas → **O(entities)**.
- A new compute `c_revoxelize_detached.glsl` binds the current pool's resident locals + its quat, computes `worldCellForGridVoxel(local, offset, {t=0, rot=canvasQuat, s=1})` **bit-identically to P1's CPU math** (`roundHalfUp` parity), and writes binding 5 (`VoxelPositionBuffer`) `[0, liveCount)` for that pool.
- **Dispatched from STAGE_1's per-canvas tick, in place of `flushStaticPositionRanges`** for `reVoxelize_` canvases — so binding 5 is filled and consumed within that canvas's tick before the next canvas overwrites it (respects the single-instance binding-5 constraint).

Why this and not #1396: #1396's per-voxel transform-slot indirection is for the **heterogeneous**-transform case (skeletal). A re-voxelize pool's voxels all share **one** transform (the canvas quat), so per-pool-resident-locals + a single quat is both correct for multi-canvas **and** simpler (no per-voxel slot). Rejected alternatives: a new per-canvas component (more plumbing, no benefit over pool-ownership since the pool already owns the CPU locals); a single concatenated buffer + offset table (a new drift-prone offset invariant).

## Affected files
- `component_voxel_pool.hpp` — resident locals/offsets SSBO (GPU-RAII), seeded at allocation, re-seeded on mutation
- new `engine/render/src/shaders/c_revoxelize_detached.glsl` (+ a shared `worldCellForGridVoxel` GLSL helper mirroring the CPU math)
- `system_voxel_to_trixel.hpp` — STAGE_1 per-canvas tick: dispatch the compute for `reVoxelize_` canvases in place of `flushStaticPositionRanges`
- `ir_system_types.hpp` — `SystemName` entry if a standalone system rather than folded into STAGE_1

## Acceptance criteria
- Visually **byte-identical to P1** for both the asymmetric solid AND the cube across a full spin (both pools fill binding 5 correctly — no clobber).
- Per-frame upload is O(entities) (one quat/canvas), not O(authored voxels); GPU dispatch cost measured (run `optimize`).
- The two-canvas demo (L-prism + cube) renders both correctly — the multi-canvas case the original plan failed.
- GLSL only; Metal in P5.

## Sequencing
Stacked on P1 (#1561), now `fleet:approved` + mergeable — P1's pool/demo model is **frozen**. Land P2 on merged P1 (rebase the stack when P1 merges). The resident-buffer model above is robust to P1's exact pool details.

## Gotchas
- Re-seed the resident locals only on pool mutation — a per-frame re-seed silently reverts to O(authored voxels) (the exact trap the original plan fell into).
- `worldCellForGridVoxel` GLSL must match the CPU `roundHalfUp` half-integer classification bit-for-bit, or P2 diverges from P1.

## Verification
`fleet-build --target IRCanvasStress`; `fleet-run`; diff frames vs P1 (byte-identical, both solids); `optimize` on the dispatch; confirm per-frame upload is one quat/canvas.
