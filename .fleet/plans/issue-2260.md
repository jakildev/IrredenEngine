## Plan: render: fog vision Z cost — XY radius with height-penalized reveal

- **Issue:** #2260
- **Model:** opus
- **Date:** 2026-07-05

### Scope

Add an observer height + Z-cost term to the analytic vision circles so the effective reveal distance is `dist_xy + zCost * |z - observerZ|`. Verified current state: `FrameDataFogObservers` (component_canvas_fog_of_war.hpp) uploads `vec4 visionCircles[8] = (x, y, radius, edgeSoftness)` + 2 ints; consumers are `c_fog_to_trixel` (per-pixel reveal + rim fade), `c_voxel_to_trixel_stage_{1,2}` (`fogColumnReveal` / `fogColumnRevealNearest`, per-COLUMN), and `c_voxel_visibility_compact` (per-column cull margin) on both backends. Columns span all z, so only the per-PIXEL and per-VOXEL tests can apply the z term; column tests must stay z-free (conservative keep).

### Approach

1. `component_canvas_fog_of_war.hpp`: extend `FrameDataFogObservers` with `vec4 visionCircleHeights[kMaxFogVisionCircles] = (observerZ, zCost, 0, 0)` appended AFTER the existing fields so the existing member offsets are unchanged; bump the `subData` size. `setVisionCircle(x, y, radius, edgeSoftness = default, observerZ = 0, zCost = 0)` — zCost 0 is the back-compat default.
2. Mirror the block extension in all consumer shaders' std140/MSL struct declarations (GLSL: c_fog_to_trixel, c_voxel_to_trixel_stage_1, c_voxel_to_trixel_stage_2, c_voxel_visibility_compact; Metal twins). Column-level functions (`fogColumnReveal`, `fogColumnRevealNearest`, the compact's cull) do NOT read the new array — a column is kept if its best-case z (zero cost) could reveal, which is exactly the current math. Comment this invariant at each site.
3. `c_fog_to_trixel`: per-pixel reveal becomes `fogVisionCircleRevealZ(pos3D, circle, heights, aa)` = existing curve evaluated at `dist_xy + heights.y * abs(pos3D.z - heights.x)`; the rim fade's `hardDistPastRim` uses the same effective distance so the fade follows the penalized boundary. `fogVisionCircleReveal` (ir_iso_common) keeps its 2D signature for the column callers; add the Z variant beside it.
4. `c_voxel_to_trixel_stage_{1,2}` per-VOXEL clip (#2102 own-column drop path where the voxel's z is known): apply the z term with the voxel's own z so boundary objects clip consistently with the pixel reveal; the nearest-cell KEEP widening stays z-free (conservative).
5. Lua surface: extend the game-side `IRFog.setVision` binding pass-through with optional `(observerZ, zCost)` trailing args (engine-side binding if it lives in engine/script, else note for the game repo).
6. Demo + gate: fog_demo `--edge-zcost` — same scene, `setVisionCircle(0, 0, kEdgeVisionRadius, 0, /*observerZ=*/4.5f, /*zCost=*/1.0f)`: the pillars' tops (z far above the floor) fade out while their bases stay revealed at the same XY distance — the height-penalty readout. New render-verify `extra_run` (zoom5/zoom9) + blessed macOS refs.
7. Full render-verify: all existing shots byte-identical (every existing caller passes zCost 0 → the new term is exactly 0).

### Affected files

- `engine/prefabs/irreden/render/components/component_canvas_fog_of_war.hpp` — struct + setVisionCircle signature
- `engine/prefabs/irreden/render/systems/system_fog_to_trixel.hpp` + `system_voxel_to_trixel.hpp` — upload size (shared UBO write sites)
- `engine/render/src/shaders/{c_fog_to_trixel,c_voxel_to_trixel_stage_1,c_voxel_to_trixel_stage_2,c_voxel_visibility_compact}.glsl` + Metal twins — struct mirror; z-aware reveal at pixel/voxel granularity
- `engine/render/src/shaders/ir_iso_common.{glsl,metal}` — `fogVisionCircleRevealZ`
- `creations/demos/fog_demo/main.cpp` + `test/references/` — `--edge-zcost` scene, manifest extra run, refs

### Acceptance criteria

- zCost = 0: render-verify byte-identical across all existing fog_demo shots (both backends' twins mirrored; Linux smoke via labels).
- `--edge-zcost`: pillar tops at equal XY distance fade harder than ground; committed refs gate it deterministically.
- No column near the rim drops earlier than today (conservative column tests) — verified by the edge scenes staying hole-free.

### Gotchas

- The UBO rides binding 27 as a transient re-bind (Metal buffer table is full); growing the struct only changes the upload SIZE, not the binding — but every consumer's struct declaration must grow in the same PR or the stale-layout consumer reads garbage past the old size.
- `kFogVisionEdgeDefault` AA and the #2102 shared-curve invariant (floor pixel reveal == voxel clip curve) must hold for the z-augmented distance too — use one shared helper, no per-shader reimplementation.
- The stage-1/2 cut-face emission keys on COLUMN reveal (z-free); a z-penalized pixel reveal above an un-penalized column keep is fine (pixels fade inside kept geometry), but the reverse (column dropped while pixels would reveal) must remain impossible — hence best-case-z column tests.
- Explored grid memory is 2D by design; the z term applies only to live circles, never the grid state.
