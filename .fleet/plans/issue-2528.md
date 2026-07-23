# Plan: engine: deterministic World teardown — reset `g_world` when `gameLoop` returns

- **Issue:** #2528
- **Model:** opus
- **Date:** 2026-07-23

## Scope

Deterministic `World` teardown for the standard creation entry path:
`IREngine::gameLoop()` destroys the world when the loop exits, instead of
leaving `~World()` to process-exit static destruction. No change to `World`
itself, `end()`, or manager destruction order.

## Approach

1. `engine/include/irreden/ir_engine.hpp::gameLoop()`: after
   `getWorld().gameLoop()` returns, `g_world.reset();` with a comment stating
   the constraint (teardown must run while `main` is on the stack and the
   graphics driver is loaded — the #2031 class).
2. Leave `World::end()` untouched — its in-loop GPU teardown remains the
   canonical device-resource release point, and it keeps release ordering
   independent of this change.
3. Docs: update the `engine/world/CLAUDE.md` gotcha (the rule stays; the
   rationale notes `~World()` now runs at `gameLoop()`-tail for the `IREngine`
   path) and `engine/CLAUDE.md` §"Manager globals" (managers valid until
   `gameLoop()` returns; post-loop access asserts in debug).
4. Audit: grep every in-tree creation `main` for engine calls after
   `IREngine::gameLoop()`. State the contract in the PR body for out-of-tree
   consumers: post-`gameLoop` engine access was never supported (GPU resources
   were already gone via `end()`); it now fails loudly in debug.

## Affected files

- `engine/include/irreden/ir_engine.hpp`
- `engine/world/CLAUDE.md`, `engine/CLAUDE.md` (doc hunks only)

## Acceptance criteria

As in the issue body. Verification: `fleet-build` + `fleet-run
--auto-screenshot` to clean process exit for `IRShapeDebug`, one Lua-driven
creation, and one video-capture-enabled run; exit codes checked (a teardown
crash surfaces as a non-zero exit after the shots pass).

## Gotchas

- The intentionally-leaked singletons (`LoggerSpd`, the Metal runtime state)
  are leaked precisely to survive shutdown ordering — unaffected; do not "fix"
  them in this PR.
- The prefab-name registry documented as surviving `World` shutdown
  (`engine/script/CLAUDE.md`) keeps that property — it is process-scoped by
  design; unaffected.
- gtest targets that construct `World` directly own their lifetime already —
  out of scope, and part of why `end()`'s contract must not weaken.
- If a demo hangs or crashes at exit on one backend only, the failure is in
  that backend's destructor ordering relative to the window — report per-host,
  don't paper over with an early return.

## Implementation notes (deltas found while executing)

- **No in-tree `World` is constructed outside `IREngine::g_world`.** The
  plan's gtest gotcha anticipated direct-`World` fixtures; a tree-wide sweep
  (`kTestLuaConfig`, `unique_ptr<World>`, `World <ident>(`) found none — the
  gtest targets use manager-level fixtures. `end()` still must not weaken:
  the reasons that survive are the exception path (`World::gameLoop()`'s catch
  block calls `end()` and rethrows, skipping the reset) and out-of-tree
  owners, not in-tree test fixtures.
- **`~World()`'s own comment asserted the now-false fact** that the dtor runs
  at static destruction. Corrected in `engine/world/src/world.cpp`; this is a
  comment-only hunk, not a change to `end()`.
- **Exception path is deliberately unchanged.** `g_world.reset()` is a plain
  post-call statement, not an RAII scope, so a throwing `gameLoop()` still
  leaves the reset unrun. That is not a regression: `end()` has already run in
  the catch block, and an exception escaping `main` terminates without running
  static destructors at all.
