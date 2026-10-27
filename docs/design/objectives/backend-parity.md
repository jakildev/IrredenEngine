# Objective: OpenGL and Metal are functionally interchangeable

**Status:** active

## Outcome
A feature landing on one render backend is expected — and verified — on
both. Choosing a host never changes what the engine can render, only how
fast the developer's inner loop runs.

## Done means
- [ ] Shader counterpart coverage stays complete: every `c_*` compute and
  `ir_*` include has an exact 1:1 counterpart; raster `v_`/`f_` pairs map
  to their bundled `.metal` equivalents; the `backend-parity` audit
  reports zero unexplained gaps.
- [ ] Detached (world-placed) rotating entities receive and cast per-axis
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
`engine/render/src/shaders/` and `shaders/metal/` (`c_*` and `ir_*` are
1:1 by name; raster `v_`/`f_` stages bundle into a bare-named `.metal`).
Five files have no counterpart by design and are recorded with their reasons
in `parity_allowlist.json`: the source-face scatter stages bundled in
`ir_source_face_scatter_body.metal`, the lighting frame-data block
co-located in `ir_world_lighting.metal`, and the Metal-only per-axis scatter
interface. `scripts/shader_parity_audit.py` checks this, and the
`shader-parity` workflow runs it on every shader PR and master push. The
`backend-parity` skill carries the port procedure and the
no-port-without-target-backend-smoke rule. What's missing is wired
per-axis lighting for detached rotation (both backends) and the GL half
of the perf story.

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-07-20 | — | objective seeded |
| 2026-10-26 | #4729 | shader counterpart audit and `shader-parity` workflow; row 3 done |
