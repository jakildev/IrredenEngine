# Plan: detached re-voxelize — GPU scatter fill (P2)

- **Issue:** #1556
- **Model:** opus
- **Date:** 2026-06-06
- **Epic:** #1553 — see `~/.fleet/plans/issue-1553.md`
- **Blocked by:** #1555

## Scope
Replace P1's CPU per-frame cell-fill with a GPU scatter compute dispatch; O(visible) on the GPU instead of O(authored) on the CPU.

## Affected files
- New compute shader `c_*scatter*.glsl` (+ a shared `worldCellForGridVoxel` GLSL helper mirroring the CPU math)
- `engine/prefabs/irreden/render/systems/system_update_voxel_positions_gpu.hpp` — reuse transform-slot indirection; add/sequence the scatter dispatch before STAGE_1
- `engine/system/include/irreden/ir_system_types.hpp` — `SystemName` entry if a new system
- `component_voxel_pool.hpp` — binding-5 population from the scatter

## Approach
Per the epic plan §P2. Reuse #1396 (binding 17 local + 18 transforms) → world pos; scatter `floor(worldPos+0.5)` into the private pool's binding-5 cell with `atomicMin` on depth.

## Acceptance criteria
Byte-identical to P1 visually; CPU cost O(entities); GPU dispatch measured; multi-canvas binding-5 constraint respected; GLSL only.

## Gotchas
Binding 5/17/18 single-instance — sequence per-canvas (`lastUploadedCanvas_`). Keep aliasing resolution to P3.

## Verification
`fleet-build --target IRCanvasStress`; `fleet-run`; diff frames vs P1 (byte-identical); run `optimize` on the scatter dispatch.
