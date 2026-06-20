# Plan: render: consolidate CPU↔GPU rounding & iso math (roundHalfUp / pos3DtoDistance / glm→IRMath)

- **Issue:** #1923
- **Date:** 2026-06-19

**Model:** opus
**Part of:** epic #1881 (rotated-voxel correctness under camera Z-yaw)
**Blocked by:** (none) — touches `ir_iso_common` + trixel shaders shared with #1883/#1884; per #1881's one-at-a-time rule, sequence AFTER the active per-axis work lands to avoid conflicts.
**Related:** #1883, #1884 (shared shader files).

## Why (motivation)
Coordinate/rounding math is duplicated and inconsistent across CPU / GLSL / Metal — the drift class that produces rotation jitter and depth bugs. Unifying it removes a category of "shapes vibrate"-style bugs at the source (the explicit ask: "less bugs like this in the future").

Verified inconsistencies:
- **Mixed rounding in one file:** `engine/render/src/shaders/c_voxel_to_trixel_stage_1.glsl` uses raw `round()` (`:248,262,272,302`) alongside `roundHalfUp()` (`:142`); `:284`'s own comment contrasts the detached path's `roundHalfUp(dot(pos,(1,1,1)))` with the world path's bare `round`+sum. Raw GLSL `round()` is implementation-defined at half-integers; `IRMath::roundHalfUp` = `floor(x+0.5)` is the CPU↔GPU contract. Half-integer positions (subdivision scaling) can flip cells between CPU and GPU → a jitter class.
- **Inlined iso math:** `pos3DtoDistance` (`x+y+z`) inlined instead of the helper in ~5 shader sites (`c_shapes_to_trixel`, `c_voxel_to_trixel_stage_{1,2}`, `v_peraxis_scatter`) — the rule forbids inlining iso equations.
- **glm outside engine/math:** `engine/render/src/ir_render.cpp` (`glm::floor` `:133-134, :151`), `engine/render/src/render_manager.cpp` (`glm::length` `:560`) — should route through IRMath (picking-coordinate consistency).

(Out of scope — verified NOT a defect: the per-axis `encodeDepthWithFaceFrac` / `scatterCompositeDepthKey` and scatter/resolve shaders **do** exist in canonical Metal source; no Metal parity gap.)

## Scope
Sweep the voxel_to_trixel_stage_1/2, per-axis scatter/resolve, particle, and visibility shaders (GLSL + Metal): `round()` → `roundHalfUp()` for every position→cell handshake; inlined `pos3DtoDistance` → the helper; migrate the cited `glm::` calls to IRMath. Verify CPU / GLSL / Metal agree.

## Acceptance criteria
- Every position→cell rounding in the trixel / per-axis / particle / visibility shaders uses `roundHalfUp` (or a documented, justified exception); no bare `round()` for cell handshakes.
- No inlined iso-projection equations in shaders (all via the helper).
- The cited `glm::` / `std::` math in `ir_render.cpp` + `render_manager.cpp` routes through IRMath; `.fleet/status/glm-deviations.md` updated.
- Rotated-solidity + jitter harness: no regression at any yaw; ideally the half-integer jitter class measurably shrinks.
- Cardinal fast path byte-identical; both backends.

## Files (start)
`engine/render/src/shaders/c_voxel_to_trixel_stage_{1,2}.glsl` + Metal mirrors; `c_resolve_per_axis_screen_depth.glsl`, `v_peraxis_scatter.glsl`, `c_render_*_particles_to_trixel.glsl`, `c_voxel_visibility_compact.glsl` (+ Metal); `engine/render/src/shaders/ir_iso_common.{glsl,metal}`; `engine/render/src/ir_render.cpp:133-151`; `engine/render/src/render_manager.cpp:560`; `.fleet/status/glm-deviations.md`.

## Gotchas
- Shared `ir_iso_common` + trixel shaders are also touched by #1883/#1884 — serialize per the epic's one-at-a-time rule.
- A `round()`→`roundHalfUp()` swap CAN change a few pixels at exact half-integers — that's the point (CPU↔GPU agreement), but re-baseline the references and confirm it's an improvement, not a regression.
- Keep the cardinal fast path byte-identical.

## References
Epic #1881; `.claude/rules/cpp-math.md` (IRMath rule); `docs/design/per-axis-trixel-canvas-rotation.md` (roundHalfUp CPU/GPU contract).
