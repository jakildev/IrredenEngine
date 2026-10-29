# Objective: a complete creation is authorable in Lua without touching C++

**Status:** active

## Outcome
A creation author builds a game-like creation — components, systems,
pipelines, input, behavior — entirely in Lua, at CODEGEN-native speed,
with the C++ engine consumed as primitives rather than edited.

## Done means
- [ ] EVAL-mode canonical-grid (16³/64³) perf numbers captured on the
  Linux fleet host (the repeatedly-flagged mechanical follow-up in
  `docs/design/lua-driven-ecs.md`).
- [ ] The G1–G4 authoring gaps close far enough that a many-agent
  navigation/steering demo is authorable in Lua: structured/container
  field types, shared-index capability, control flow for iterative
  kernels, structural change in a tick (`lua-driven-ecs.md` follow-ups).
- [ ] The binding-automation follow-ups land: the shared
  `registerStandardBindings()` header (`docs/design/lua-binding-automation.md`
  task 1), and the `IR_BIND_LUA_COMPONENT` macro only if that doc's task-3
  trigger fires (a PR adding three or more trivial `_lua.hpp` files at
  once).
- [ ] A demo authored 100% via Lua schema ships and participates in the
  verify culture (render-verify or gui-verify, not just "it runs").

## Non-goals
Replacing C++ for cross-entity stateful solvers — the authoring boundary
ruled in #1400 stands (per-entity data-parallel arithmetic → Lua/CODEGEN;
solvers → C++ primitives consumed by batched query). Component-schema
hot-reload (explicitly deferred). Codegen'ing the binding files
themselves (rejected in `lua-binding-automation.md`).

## Current state
The two-path architecture holds: CODEGEN (`cmake/lua_codegen/`) emits typed
C++ from `.lua` schemas at build time and EVAL (LuaJIT + sol2) keeps
hot-reload alive via `IRSystem.replaceSystemBody`. The only parity figure
is still the CODEGEN 64³ wave tick at ~0.46× of the C++ baseline on macOS
Metal (PR #563; `creations/demos/lua_perf_grid/`); no EVAL-mode or
Linux-host numbers exist, though the harness does
(`scripts/perf/lua_cpp_parity.py`, `perf_grid_matrix.sh --target both`), so
row 1 is open. Of the G1–G4 gaps, G2 shipped as `C_SpatialIndex` and
`IRSpatial.queryRadius` (PR #1421); G1a shipped vec3/ivec3 packed fields in
both paths (PR #1432) and vec4/quat in EVAL only (PR #2198); G1b inline
arrays, G3 pathfinding and bounded iteration (CODEGEN still rejects
`while`/`repeat`/`break`), and G4's deferred component add/remove with
CODEGEN lowering are unbuilt (only `deferredCreate`, `deferredDestroy`,
`deferredCall` and `deferredDestroyTree` are bound), and no navigation or
steering demo exists, so row 2 is open. Binding boilerplate grew: 51
hand-written `*_lua.hpp` files (2670 lines), and `Color`/`vec3`/`ivec3` are
hand-registered in five creations; #4764 files the
`registerStandardBindings` header, and the `IR_BIND_LUA_COMPONENT` macro is
`lua-binding-automation.md`'s conditional task 3, which that doc rules
against until a PR adds three or more trivial binding files at once, so row
3's wording now follows the doc. `lua_perf_grid` is the sole codegen-driven
creation and has no verify coverage; `lua_pipeline_demo` (render-verify,
macOS) and `lua_widgets` (25 GUI shots) are EVAL-path with C++ beyond the
entry point, so row 4 is open.

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-05-31 | #1412 / PR #1421 | G2: `C_SpatialIndex` and `IRSpatial.queryRadius` |
| 2026-06-01 | #1368 / PR #1432 | G1a: vec3/ivec3 packed fields in EVAL and CODEGEN |
| 2026-07-04 | PR #2198 | G1a: vec4/quat fields, EVAL only |
| 2026-07-20 | — | objective seeded |
| 2026-10-27 | sweep; #4764 | re-verified against master `ee85797c0`; binding count 51 files / 2670 lines; #4764 filed for the shared bindings header (row 3); row 3 wording aligned with the design doc's conditional macro |
