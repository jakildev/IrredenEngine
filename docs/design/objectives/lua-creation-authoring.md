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
- [ ] The binding-automation follow-ups land: shared
  `registerStandardBindings()` header and the `IR_BIND_LUA_COMPONENT`
  macro (`docs/design/lua-binding-automation.md` — plan exists, tasks
  unstarted).
- [ ] A demo authored 100% via Lua schema ships and participates in the
  verify culture (render-verify or gui-verify, not just "it runs").

## Non-goals
Replacing C++ for cross-entity stateful solvers — the authoring boundary
ruled in #1400 stands (per-entity data-parallel arithmetic → Lua/CODEGEN;
solvers → C++ primitives consumed by batched query). Component-schema
hot-reload (explicitly deferred). Codegen'ing the binding files
themselves (rejected in `lua-binding-automation.md`).

## Current state
The two-path architecture shipped and met its gate: CODEGEN (default,
build-time typed C++ from `.lua` schemas) runs the 64³ wave tick at
~0.46× of the C++ baseline (`creations/demos/lua_perf_grid/`), and EVAL
(LuaJIT + sol2) keeps hot-reload alive via
`IRSystem.replaceSystemBody`. Components, batched systems, pipeline
composition, enums, and modifier bindings are authorable today; 39
hand-written `*_lua.hpp` binding files (~1.6k lines) carry the surface.
The gaps are EVAL perf validation, the nav/steering feature classes
(G1–G4), and the binding-boilerplate reduction.

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-07-20 | — | objective seeded |
