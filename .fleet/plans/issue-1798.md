# Plan: render — Hi-Z (max-depth mip chain) build stage (occlusion cull child 1/3)

- **Issue:** #1798 (child 1/3 of #1294)
- **Parent plan:** `.fleet/plans/issue-1294.md` — shared design, measurement gate (0.97 cleared), cross-system audit, correctness constraints
- **Design:** `docs/design/voxel-occlusion-culling.md` § Implementation sketch step 1
- **Model:** opus
- **Date:** 2026-06-13

## Verified current state

- `voxelStage1` GPU = 32.1ms = 73% of the IRPerfGrid `voxel_set` zoom8 frame
  (post-#1778, measured this session). `occludedExposedFraction` = 0.97 (gate
  cleared — see parent plan).
- No Hi-Z buffer exists today; `triangleCanvasDistances` is a flat R32I image
  (`component_triangle_canvas_textures.hpp`, binding 1). The voxel path runs in
  `system_voxel_to_trixel.hpp` (stage 1 = atomicMin distance write, stage 2 =
  color write where the voxel won).
- #1278/#1288 merged — raster baseline stable.

## Approach (single path)

1. Add a mipped R32I "Hi-Z" texture to `C_TriangleCanvasTextures` (max-depth
   pyramid; mip 0 = full-res copy or direct source of `triangleCanvasDistances`).
2. New `COMPUTE_DISTANCE_HIZ` system + `c_build_distance_hiz.{glsl,metal}`:
   downsample-**max** each level (a coarse texel = the farthest depth over its 4
   children, so "anything strictly behind it is hidden"). New `SystemName` enum
   entry. Per-canvas.
3. Schedule it in the render pipeline **after stage 2 + AO** complete (the
   distances are final) and before the next frame's cull (child 2) reads it. Use
   last frame's Hi-Z as the cull source (one-frame lag, design § 3).

## Affected files

- `engine/render/src/shaders/c_build_distance_hiz.{glsl,metal}` — new
- `engine/prefabs/irreden/render/components/component_triangle_canvas_textures.hpp` — Hi-Z texture
- `engine/system/include/irreden/system/ir_system_types.hpp` — `SystemName` entry
- `engine/prefabs/irreden/render/systems/` — new system; pipeline registration in `creations/demos/perf_grid/main.cpp` render pipeline (after AO)

## Acceptance criteria

- Hi-Z mip chain built each frame; each coarser texel = max of its 4 children.
- Renders unchanged (Hi-Z is consumed next frame by child 2; this PR only
  produces it).
- Builds clean; `render-debug-loop` + `attach-screenshots` on both hosts.

## Gotchas

- **Shadow-feeder coverage** — the Hi-Z must cover the shadow-feeder-widened
  bounds that child 2 samples, not just the visible viewport, or sun shadows lose
  off-screen casters (design's #1 hazard). Confirm the distance texture extent
  already includes the swept region (it does — the sun-shadow bake reads it).
- Max (not min) downsample — a min pyramid would cull visible voxels (holes).
