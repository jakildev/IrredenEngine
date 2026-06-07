# Plan: detached re-voxelize — render-verify + retire forward-scatter (P6)

- **Issue:** #1560
- **Model:** opus
- **Date:** 2026-06-06
- **Epic:** #1553 — see `.fleet/plans/issue-1553.md`
- **Blocked by:** #1559

## Scope
Commit render-verify baselines for the re-voxelize detached path and retire the now-superseded detached per-axis forward-scatter, making re-voxelize the sole detached SO(3) path.

## Affected files
- Remove: `v_peraxis_scatter_detached.{glsl,metal}`, the `PerAxisScatterDetachedProgram` path, `syncAllocationToDetachedEntities` / `kMaxDetachedRotatingCanvases` (detached only)
- `per_axis_canvas.hpp`, `system_entity_canvas_to_framebuffer.hpp` — remove the detached forward-scatter branches
- `docs/design/per-axis-trixel-canvas-rotation.md`, `docs/design/voxel-face-rasterization.md`, `engine/prefabs/irreden/render/CLAUDE.md` — flip the model from "committed epic" to "shipped"
- render-verify reference images

## Approach
Per the epic plan §P6. Retire ONLY the detached consumer of the per-axis machinery — the camera/main-canvas smooth-yaw path keeps it. (No #1551 stopgap to remove — it was closed as superseded, never merged.)

## Acceptance criteria
- Forward-scatter removed for detached; re-voxelize sole path; cube + asymmetric both clean.
- The camera/main-canvas per-axis path is untouched and byte-identical.
- Cardinal/identity byte-identical; render-verify green; docs updated.

## Gotchas
Cross-check every `isDetached()`-gated branch — the per-axis machinery is shared with the camera path; retire only the detached consumer.

## Verification
`fleet-build`; `render-verify` on the re-voxelize shots; sweep the camera smooth-yaw path for byte-identical; build both backends.
