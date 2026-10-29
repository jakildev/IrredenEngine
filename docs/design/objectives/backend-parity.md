# Objective: OpenGL and Metal are functionally interchangeable

**Status:** active

## Outcome
A feature landing on one render backend is expected — and verified — on
both. Choosing a host never changes what the engine can render, only how
fast the developer's inner loop runs.

## Done means
- [x] Shader counterpart coverage stays complete: every `c_*` compute and
  `ir_*` include has an exact 1:1 counterpart; raster `v_`/`f_` pairs map
  to their bundled `.metal` equivalents; the `backend-parity` audit
  reports zero unexplained gaps.
- [x] Detached (world-placed) rotating entities receive and cast per-axis
  lighting on both backends (#1375 receive, #1376 cast) — today they
  composite raw unlit voxel color on both.
- [x] A CI parity guard exists: a PR adding a `.glsl` without a matching
  `.metal` (or an acknowledged deferral + follow-up reference) fails a
  workflow check instead of relying on reviewer memory.
- [ ] Cross-backend perf comparison captured: `perf_grid` numbers from an
  OpenGL host beside the committed Metal baseline
  (`docs/perf/metal_perf_grid_baseline.md` admits the GL side is
  uncaptured).

## Non-goals
Identical performance across backends (parity is functional, not
frame-time); additional backends (Vulkan / WebGPU); porting the
macOS-only bridge files (`metal_cocoa_bridge.mm` et al.) — those are
by-design asymmetric.

## Current state
Shader coverage is paired file for file: 133 GLSL and 123 Metal files in
`engine/render/src/shaders/` and `shaders/metal/` — 58/58 `c_*` compute
stages, the `ir_*` includes paired by name, and 16 raster `.metal` bundles
covering 9 `v_`/`f_` pairs and 7 fragment-only stages. Five files have no
counterpart by design and are recorded with their reasons in
`parity_allowlist.json`: the source-face scatter stages bundled in
`ir_source_face_scatter_body.metal`, the lighting frame-data block
co-located in `ir_world_lighting.metal`, and the Metal-only per-axis scatter
interface. `scripts/shader_parity_audit.py` reports clean on master, and the
`shader-parity` workflow runs it on every shader PR and master push, so rows
1 and 3 verify. Detached world-placed entities rotate through
`RotationMode::DETACHED_REVOXELIZE` and receive world AO, sun shadow and
light volume (`system_lighting_to_trixel.hpp`,
`c_lighting_to_trixel_body.{glsl,metal}`) and cast into the shared sun map
(`system_bake_sun_shadow_map.hpp`,
`c_resolve_world_placed_depth.{glsl,metal}`) on both backends, with the
`so3_wide_parity` and `revoxelize_solids` canvas_stress references blessed on
all three presets; the plain forward-scatter `DETACHED` mode casts nothing
by design (`engine/prefabs/irreden/render/CLAUDE.md` §Rotation modes), and
only the screen-locked overlay path composites raw albedo, so row 2
verifies. The `backend-parity` skill carries the port procedure and the
no-port-without-target-backend-smoke rule. What's missing is the GL half of
the perf story: hosted-CI llvmpipe baselines exist on the `perf-baseline`
branch, but no hardware OpenGL-host `perf_grid` capture sits beside
`docs/perf/metal_perf_grid_baseline.md`, which still records the GL side as
uncaptured (#3814 and #4155 are parked on a quieted Windows host).

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-06-09 | #1576 (supersedes #1375) / PR #1617 | detached re-voxelize world receive: sun shadow and light volume, both backends |
| 2026-06-11 | #1596 (supersedes #1376) / PR #1626 | detached world sun-shadow cast via resolve-then-bake |
| 2026-07-09 | #2322 / PR #2328 | world-placed detached canvases receive world lighting by default; row 2 verifies |
| 2026-07-20 | — | objective seeded |
| 2026-08-22 | #2816 / PR #3025 | sub-cell fraction applied on the per-axis cast bridge |
| 2026-10-26 | #4729 | shader counterpart audit and `shader-parity` workflow; row 3 done |
| 2026-10-27 | #4729 / PR #4751 | the audit and workflow above merged; rows 1 and 3 verify; sweep re-verified every row against master `ee85797c0` |
