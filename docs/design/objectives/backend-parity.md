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
- [ ] A CI parity guard exists: a PR adding a `.glsl` without a matching
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
Coverage is effectively complete today: 33/33 compute shaders and 6/6
shared includes are 1:1 (`engine/render/src/shaders/` vs `shaders/metal/`);
the apparent 49-vs-44 file delta is the raster pair-vs-bundled convention,
not missing ports. The `backend-parity` skill carries the audit loop and
the no-port-without-target-backend-smoke rule. What's missing is wired
per-axis lighting for detached rotation (both backends), any CI-side
guard, and the GL half of the perf story.

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-07-20 | — | objective seeded |
