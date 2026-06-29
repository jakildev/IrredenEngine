# Plan: #2059 — migrate auto-screenshot-only + no-arg demos to IRArgs (P2)

- **Issue:** #2059
- **Model:** sonnet
- **Date:** 2026-06-26
- **Epic:** #2057 — see `.fleet/plans/issue-2057.md`
- **Blocked by:** #2058

## Verified current state
Each listed demo's `main()` calls
`IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames)` then
`IREngine::init(argv[0])`, and later gates a `createAutoScreenshotSystem`
block on `g_autoWarmupFrames > 0` (canonical example:
`creations/demos/day_cycle/main.cpp`). No other flags. Assumes P1 (#2058)
landed `IREngine::init(argc,argv)` + `IREngine::args()`.

## Affected files
The 21 auto-screenshot-only `main*.cpp` + 3 no-arg `main*.cpp` listed in the
issue body (one mechanical edit each). No shared headers.

## Approach
Per file: `init(argv[0])` → `init(argc, argv)`; delete the
`parseAutoScreenshotArgv` line; assign warmup frames from
`IREngine::args().autoScreenshotWarmupFrames()` after init (keep the local
or global variable, assigned once). Keep every `createAutoScreenshotSystem`
block intact.

### Variable name variations
Most demos use `g_autoWarmupFrames`; a few use `autoWarmupFrames` (local);
some use namespace-qualified forms (`IRDockspaceDemo::g_autoWarmupFrames`,
`IRWidgetsDemo::g_autoWarmupFrames`, `IRLuaWidgets::g_autoWarmupFrames`).
Five demos use `init(argv[0], "config.lua")` → `init(argc, argv, "config.lua")`.

## Sibling reconciliation
Touches ONLY the 24 files listed — perf_grid/shape_debug (P3), lighting (P4),
game/MIDI/editors (P5) are explicitly out of scope so the diffs don't overlap.
Leaves `parseAutoScreenshotArgv` defined (P6 deletes it once all callers gone).

## Verification
- All 24 targets build clean.
- `day_cycle --auto-screenshot 12` and `sprite_demo --auto-screenshot 12`
  capture; `day_cycle --help` exits 0.
