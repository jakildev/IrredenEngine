# Plan — #2572: EntityEventHandlers unrefs Lua handlers after World teardown (exit SIGSEGV)

## Scope

Make entity-event handler teardown deterministic: the Lua handler references
in `IRSystem::EntityEventHandlers` must be released while the Lua VM is still
alive, so the process-exit destruction of the static registry touches only
empty vectors. No change to the runtime dispatch surface.

## Approach

1. Add `EntityEventHandlers::clear()` — drops all four `HandlerEntry`
   vectors (destroying the `sol::protected_function`s) and leaves `nextId`
   as-is (ids never recycle within a process; cheap and unambiguous).
2. Call it at the `IREngine::gameLoop()` tail, immediately **before** the
   `g_world.reset()` that #2539 added — the VM is still alive there, and the
   engine tail is unconditional, so a creation cannot forget the cleanup.
3. This also closes the stale-handler half of the latent multi-world
   hazard: without the clear, a future second `World` in-process would
   dispatch stale handlers bound to the dead VM; with it, the registry
   starts empty. Multi-world is NOT fully clean after this fix —
   `previousHoveredEntity` (a second process-lifetime static inside
   `System<ENTITY_HOVER_DETECT>::create()`) survives into a hypothetical
   second World, whose first hover change would fire `fireUnhovered` with
   the previous World's stale id. Pre-existing and unreachable today.

Implementation note: `IREngine::gameLoop()` is header-inline in
`ir_engine.hpp`, and the handler registry lives behind a prefab header that
pulls in sol2 + `ir_render`. Including that header into `ir_engine.hpp` would
transitively widen every creation's include of the engine entry point, so the
call is routed through an out-of-line `IREngine::detail::clearEntityEventHandlers()`
defined in `engine/engine.cpp` — the same pattern the pre-existing
`detail::applyPreInitLuaConfig` uses for exactly this reason. (The plan's
original "call in `ir_engine.cpp`" wording predated confirming the tail is
header-inline.)

Alternative considered: a generic `LuaScript::onTeardown(callback)` registry
that prefab-layer statics hook at first registration. More general, but
heavier than the one deterministic call site the engine tail already offers;
revisit if a second Lua-ref-holding static appears.

## Affected files

- `engine/prefabs/irreden/input/systems/system_entity_hover_detect.hpp` —
  add `EntityEventHandlers::clear()`.
- `engine/include/irreden/ir_engine.hpp` — declare
  `detail::clearEntityEventHandlers()`; call it in inline `gameLoop()` before
  `g_world.reset()`.
- `engine/engine.cpp` — include the prefab header; define
  `detail::clearEntityEventHandlers()` calling
  `IRSystem::getEntityEventHandlers().clear()`.

## Acceptance criteria

As on the issue: clean exit with a Lua `onRightClick` registered (macOS +
Linux), unchanged dispatch during the run, registry empty post-`gameLoop`.
A/B repro: register `IRInput.onRightClick` from any creation's `main.lua`;
without the fix the run segfaults in `~EntityEventHandlers` at exit.

## Gotchas

- The clear must run **before** `g_world.reset()` — after it, the VM is
  gone and `luaL_unref` is the crash itself.
- Don't move the registry onto `System<ENTITY_HOVER_DETECT>`: Lua handlers
  register at script-load time, before the system exists — that ordering is
  why the static accessor exists.
- `IR_RELEASE` builds strip asserts, not this path — the fix is a real call,
  not an assert.
