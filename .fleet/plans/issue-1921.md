# Plan: render: camera Z-yaw should pivot about the point-of-interest, not the z=0 screen-center point

- **Issue:** #1921
- **Date:** 2026-06-19

**Model:** opus
**Part of:** epic #1881 (rotated-voxel correctness under camera Z-yaw)
**Blocked by:** (none) — independent (camera / `ir_render` path; no shared-shader conflict, can run in parallel with the other children).
**Related:** #1352 / PR #1362 (camera focus pivot) — this is the follow-up, NOT a regression (that code is intact).

## Why (motivation)
On IRShapeDebug the point the camera rotates about is wrong: shapes **arc across the screen** instead of rotating in place. Two confirmed defects in the #1362 focus-pivot at `engine/render/src/ir_render.cpp:44-60` (verified intact — NOT a regression):

1. **Pivot pinned to z=0.** `getEffectiveCameraIso()` reconstructs the focus as `IRMath::isoPixelToPos3D(cameraIso, 0.0f)` — the world point under screen center on the **z=0 plane**. Content with height (z>0) orbits its z=0 base, so tall shapes swing/arc.
2. **Pivot is the screen-center world point, not the point-of-interest.** The #1362 correction "is the identity unless the camera is BOTH panned and rotated" (per `creations/demos/shape_debug/main.cpp:134-143`). In the default un-panned scene, off-center shapes (placed at x=16, 32, …) all orbit the **world origin** — the "wrong focus" the user observes. The unshipped slice of #1352 was a cursor/selection-anchored pivot.

## Scope
Make camera Z-yaw rotate about the actual point-of-interest at its true depth, so content rotates in place regardless of pan/height.

## Approach (planner settles the pivot policy)
- **Pivot DEPTH:** derive the focus depth from content under the screen center (e.g. the #1910 composite depth-probe at screen center, or the hovered/selected entity's depth) instead of hardcoding `0.0f`.
- **Pivot TARGET** (design choice for the plan): (a) cursor-anchored, (b) selected-entity-anchored, (c) scene/content centroid. Recommended default: cursor-anchored with a scene-centroid fallback, exposed via the existing `RotationPivotMode` enum.
- **visualYaw vs rasterYaw:** confirm the pivot re-projection uses the same yaw basis the rasterizer commits (cardinal + residual), to avoid a lurch at cardinal-bracket boundaries.

## Acceptance criteria
- Across a full yaw sweep at multiple pans and zooms, the chosen pivot point stays pinned at screen center within ≤1 game-pixel; on-screen content rotates in place (no off-center arc), including content at z>0.
- Cardinal fast path + `ORIGIN` mode byte-identical (A/B via `--pivot-origin`).
- New shape_debug shots capturing a **tall** shape rotating in place (pivot pinned) as regression coverage.
- Both backends.

## Files (start)
`engine/render/src/ir_render.cpp:44-60` (getEffectiveCameraIso); `engine/render/src/render_manager.*` (RotationPivotMode, pivot target); `engine/math/include/irreden/ir_math.hpp` (isoPixelToPos3D / pos3DtoPos2DIsoYawed); `creations/demos/shape_debug/main.cpp` (pivot shots); optionally #1910 depth-probe for focus-depth.

## Gotchas
- Iso projection degenerates near yaw ±2π/3 (det≈0) — the pan inverse already guards this in `ir_math.hpp`; the pivot path must too.
- Keep `ORIGIN` mode and the no-pan/no-rotate fast path byte-identical.

## References
#1352 / PR #1362 (focus pivot, intact); #1420 / PR #1496 (screen-relative WASD); #1910 (depth-probe, candidate focus-depth source); epic #1881.
