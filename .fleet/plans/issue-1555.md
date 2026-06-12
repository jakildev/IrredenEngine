# Plan: detached re-voxelize — CPU model proof + asymmetric demo (P1)

- **Issue:** #1555
- **Model:** opus
- **Date:** 2026-06-06
- **Epic:** #1553 — see `.fleet/plans/issue-1553.md` for full context + architecture decisions
- **Blocked by:** (none)

## Scope
Prove the detached re-voxelize model end-to-end with a CPU cell-fill. The headline phase: an asymmetric detached solid at ~45° must read as a true 3D-rotated solid (voxel centers reorganized). **Also migrate the existing `canvas_stress` detached cube fringe to re-voxelize** — #1551/#1552 (the forward-scatter cube stopgap) were closed as superseded by this epic, so P1 is the path that fixes the visible cube crumple + #1539 pop, correctly.

## Affected files
- `engine/prefabs/irreden/common/components/component_rotation_mode.hpp` — new detached re-voxelize mode/flag
- `engine/prefabs/irreden/voxel/systems/system_rebuild_grid_voxels.hpp` — extend (or sibling) to fill the detached private pool under the full rotation
- `engine/prefabs/irreden/voxel/grid_rotation.hpp` — reuse `worldCellForGridVoxel` (no new math)
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` — `buildVoxelFrameData`: route re-voxelize detached canvases through cardinal/static frame data (NOT the per-entity skew branch)
- `creations/demos/canvas_stress/main.cpp` — add an asymmetric detached rotating test solid; opt the existing detached cube fringe into re-voxelize

## Approach
Per the epic plan §P1. Crux: the re-voxelize canvas renders its private pool exactly like a static canvas (rotation lives in cells, not a deform). Bypass the octahedral + per-axis path for opted entities. Both the existing cubes and the new asymmetric solid opt into re-voxelize.

## Acceptance criteria
- Asymmetric solid reads true-3D at 45°; clean at every residual; no pop.
- The existing `canvas_stress` detached cube fringe renders via re-voxelize — clean at every pose, no #1539 pop (the cube crumple #1551 targeted, fixed correctly here).
- Non-opted entities byte-identical; GLSL only.

## Gotchas
Double-rotation if the canvas inherits skew frame data. Size the private pool for the rotated AABB (×√3). Aliasing is P3's job.

## Verification
`fleet-build --target IRCanvasStress`; `fleet-run IRCanvasStress --auto-screenshot`; inspect the asymmetric solid AND the cube fringe at mid-residual read as true rotated solids; confirm a static scene is byte-identical.
