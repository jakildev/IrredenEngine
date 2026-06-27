# Plan: P1 — engine owns the IRArgs parser; retire parseConfigPresetArg

- **Issue:** #2058
- **Model:** opus
- **Date:** 2026-06-26
- **Epic:** #2057 — see `.fleet/plans/issue-2057.md` for full context
- **Blocked by:** (none)

### Verified current state (origin/master @ a870e1e3)
- `IREngine::init(const char *argv0, const char *configFileName="config.lua")`
  — `engine/include/irreden/ir_engine.hpp:62`. Body: `weakly_canonical(argv0)`
  → set cwd → `applyPreInitLuaConfig` → construct `World` (window/GL/Metal) →
  `setupLuaBindings`. Each `main()` parses common args BEFORE this via the
  legacy helpers and threads results back by hand.
- `IREngine::parseConfigPresetArg(argc, argv)` — inline `ir_engine.hpp:90`.
- `IRArgs::Parser` ctor already pre-registers `--auto-screenshot`,
  `--config-preset`, `--help`/`-h` and exposes `autoScreenshotWarmupFrames()`
  / `configPreset()` (`engine/include/irreden/ir_args.hpp`).

### Cross-system audit — every launch target calls `IREngine::init`
Adding the `argc,argv` overload is additive, but the **old `init(argv0,...)`
overload MUST keep working** so this child is non-breaking — the ~55 callers
(demos, lighting via macro, game/irreden, MIDI via `IRMidi::run`, editors)
move to `init(argc,argv)` in P2–P5, not here. So P1 adds, never removes, that
overload. Re-grep `IREngine::init(` at impl time to confirm the count.

`parseConfigPresetArg`'s only caller is `perf_grid` (migrated in P3). To keep
P1 standalone, P1's diff also updates `perf_grid`'s single preset read to use
the engine-owned parser while deleting the helper — otherwise perf_grid won't
link until P3. **Confirm `grep -rn parseConfigPresetArg engine creations`
returns only perf_grid before deleting.**

### Affected files
- `engine/include/irreden/ir_engine.hpp` — add `args()` + `init(argc,argv)`;
  read preset via `args().configPreset()`; delete `parseConfigPresetArg`.
- `creations/demos/perf_grid/main.cpp` — repoint its one preset read (minimal).
- `creations/demos/fog_demo/main.cpp` — register `--moving-observer` on
  `IREngine::args()`, call `init(argc,argv)`, drop the local `IRArgs::Parser`.
- `engine/CLAUDE.md` — update the reference-adoption note + add the
  "no-arg targets get IRArgs for free" sentence.

### Approach
Per the issue body. Load-bearing detail: `args().parse(argc, argv)` is the
FIRST statement of `init(argc,argv)`, before cwd/Lua/World, so `--help` and
unknown-arg `exit(2)` resolve pre-window/GL/Metal.

### Implementation notes (recorded during execution)
- The engine never consumed `--config-preset` internally; `perf_grid` is the
  sole consumer (threads it into its own `applyConfigPreset`). So P1 adds no
  internal engine preset read — `args().configPreset()` simply becomes
  available to targets that adopt `init(argc,argv)`.
- `init(int,char**,...)` parses then delegates to the existing
  `init(const char*,...)` overload (no body duplication; argv0 path unchanged).
- `perf_grid` is NOT migrated to `init(argc,argv)` in P1 (its 14 custom flags
  would `exit(2)` against the common-args-only engine parse — that is P3). Its
  `--config-preset` read is folded into its existing hand-rolled loop, so the
  helper is deleted with zero behaviour change and no new standalone loop.

### Verification
- `fleet-build --target IRFogDemo` (macOS) clean; `IRFogDemo --help` exits 0
  pre-init and lists `--moving-observer`.
- `fleet-build --target IRPerfGrid` clean (proves the helper deletion links).
- A throwaway demo calling only `init(argc,argv)` shows working `--help` /
  `--auto-screenshot` / `--config-preset` with no parser code.
- `grep -rn parseConfigPresetArg engine creations` → empty.
