# Epic plan: port all launch targets to IRArgs; engine owns the common-arg parse

- **Area:** engine / creations
- **Model:** opus (epic; per-child class varies)
- **Date:** 2026-06-26
- **Follow-on to:** #2044 (framework issue) / #2045 (merged framework PR)
- **Reference adoption:** `creations/demos/fog_demo/main.cpp`
- **Convention note:** `engine/CLAUDE.md` §"CLI args go through IRArgs"

## Context

#2045 merged the `IRArgs::Parser` declarative CLI framework
(`engine/include/irreden/ir_args.hpp` + `engine/ir_args.cpp`) and migrated
`fog_demo` as the reference adoption. It was deliberately scoped as a
foundation: the two legacy scattered helpers and ~55 other launch targets
still hand-roll argv parsing. `engine/CLAUDE.md` already commits to retiring
the helpers ("being migrated onto `IRArgs` and retired (#2044 follow-ons)").

This epic finishes that migration **and** adds the ergonomic the human asked
for: a target with no custom flags should get `--help` / `--auto-screenshot` /
`--config-preset` **for free**, without constructing a parser at all — the
engine owns the parse so it "comes with the world".

### Verified current state (origin/master @ a870e1e3)

- `IREngine::init(const char *argv0, const char *configFileName="config.lua")`
  (`engine/include/irreden/ir_engine.hpp:62`) takes only the exe-path string;
  the common-arg parse happens in `main()` **before** `init`, via the legacy
  helpers, and results are threaded back in by hand.
- Legacy helper 1: `IRVideo::parseAutoScreenshotArgv(argc, argv, &warmupOut)`
  — decl `engine/video/include/irreden/video/auto_screenshot.hpp:114`, impl
  `engine/video/src/auto_screenshot.cpp:68`.
- Legacy helper 2: `IREngine::parseConfigPresetArg(argc, argv)` — inline in
  `engine/include/irreden/ir_engine.hpp:90`.
- `IRArgs::Parser`'s ctor pre-registers the engine-common args
  (`--auto-screenshot`, `--config-preset`) + `--help`/`-h`, and exposes
  `autoScreenshotWarmupFrames()` / `configPreset()`. It is **named-arg only**
  today — no positional-argument support, and no way to construct a parser
  *without* the engine-common args.

### Migration surface (inventory)

- **~37 helper-only targets** call `parseAutoScreenshotArgv` and nothing else
  (most demos, voxel_editor, the 5 game/irreden demos, most MIDI projects via
  the `IR_MIDI_PROJECT_MAIN` macro → `IRMidi::run`).
- **Custom-flag targets:** `perf_grid` (14 flags + both helpers), `shape_debug`
  (~18 flags + auto-screenshot helper), the **lighting family** (15 variants,
  all routed through one shared `lighting_demo_scene.hpp::detail::parseArgs`).
- **No-arg targets** (`default`, `z_yaw_rotation/main_mouse`, `font_maker`,
  several MIDI projects) gain `--help` for free once the engine owns the parse.
- **Standalone tools** that parse argv but do NOT run the engine loop:
  `tools/img_diff` (`--threshold`, `--ignore-alpha`, + 3 positionals),
  `tools/jitter_probe` (5 flags + N positionals), `cmake/lua_codegen`
  (`--out`, `--default-mode`, own `--help`, + N positionals).

## Engine-owned-parser design (the load-bearing decision)

- Add `IRArgs::Parser& IREngine::args()` — a process-global engine parser,
  lazily constructed with the engine-common args + `--help` pre-registered.
- `IREngine::init(int argc, char **argv, const char *configFileName)`: its
  **first** statement is `IREngine::args().parse(argc, argv)`, before the
  cwd-change / Lua config / `World` construction (window/GL/Metal). So `--help`
  and unknown-arg `exit(2)` resolve before any heavy init — the invariant
  #2045 established is preserved because the parse is init's first action.
- A no-custom-arg target calls `IREngine::init(argc, argv)` and is done.
- A custom-flag target registers on `IREngine::args()` *before* `init`, then
  reads back via `IREngine::args().getFlag(...)` etc. The single engine parse
  covers common + custom flags, so `--help` aggregates everything.
- The engine reads `configPreset()` off its own parser internally →
  `parseConfigPresetArg` is **retired in phase 1**.

## Phase decomposition (file-epic chain)

Each phase is one child, chained `Blocked by:` its predecessor where a real
dependency exists. Phase 1 is the foundation that unblocks 2–6.

### P1 — [opus] Engine owns the IRArgs parser; retire parseConfigPresetArg
Foundation. `IREngine::init(argc, argv)` parses common args first; expose
`IREngine::args()`; engine reads `configPreset()` internally; delete
`parseConfigPresetArg`. Re-point `fog_demo` + `engine/CLAUDE.md` at the
engine-owned pattern. **Cross-system audit required** (changes the
`IREngine::init` public signature — every launch target calls it). Blocks
P2–P6.

### P2 — [sonnet] Mechanical migration: auto-screenshot-only + no-arg demos
~37 targets: swap `init(argv[0])` → `init(argc, argv)`, delete the
`parseAutoScreenshotArgv` call, read warmup via
`IREngine::args().autoScreenshotWarmupFrames()`. No-arg demos just switch the
init call. Behaviour-preserving; each gains `--help`. Blocked by P1.

### P3 — [opus] Custom-flag demos: perf_grid + shape_debug
Register their 14 / ~18 flags declaratively on `IREngine::args()`; drop the
`parseArgs()` strcmp loops and the legacy helper calls. Blocked by P1.

### P4 — [sonnet] Lighting family (one shared header → 15 variants)
Migrate `lighting_demo_scene.hpp::detail::parseArgs` (+ the
`lighting_demo_main.hpp` macro) to register `--auto-profile`, `--zoom`,
`--debug-overlay`, `--no-ao`/`--ao-off` on `IREngine::args()`. One change
flips all 15 targets. Blocked by P1.

### P5 — [sonnet] Game demos + MIDI macro + editors
Update `IRMidi::run` (`midi_project_scene.hpp:215`) once → every
`IR_MIDI_PROJECT_MAIN` project inherits IRArgs + `--help`. Migrate the 5
game/irreden demos, voxel_editor, font_maker. Blocked by P1.

### P6 — [sonnet] Retire parseAutoScreenshotArgv; finalize CLAUDE.md
Once P2–P5 leave no caller, delete the helper (or make it engine-parser
private) and drop the "being migrated / retired (#2044 follow-ons)" hedge in
`engine/CLAUDE.md`. **Cross-system audit required** (helper deletion). Blocked
by P2, P3, P4, P5.

### P7 — [opus] Standalone tools + two IRArgs framework extensions
Framework first: (a) a no-common-args parser mode so `img_diff --help` doesn't
advertise `--auto-screenshot`; (b) **positional-argument support** (tools take
`baseline.png current.png …`). Then port `img_diff`, `jitter_probe`,
`lua_codegen`. Blocked by P1 (uses `IRArgs` but not the engine-owned parse).

## Dependency chain

```
P1 ─┬─ P2 ─┐
    ├─ P3 ─┤
    ├─ P4 ─┼─ P6
    ├─ P5 ─┘
    └─ P7
```

## Closing criteria

- No launch target contains a hand-rolled `strcmp(argv[i], ...)` loop.
- Both legacy helpers (`parseAutoScreenshotArgv`, `parseConfigPresetArg`)
  deleted; `engine/CLAUDE.md` hedge removed.
- A no-custom-arg target gets working `--help` / `--auto-screenshot` /
  `--config-preset` with zero parser code.
- `IRArgs` supports positional args + a no-common-args mode (tools use them).

## Steward ledger

reconciled-through: 2026-06-26
proposal-pending: none

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #2058 | open | — | P1 | 2026-06-26 |
| #2059 | open | — | P2 | 2026-06-26 |
| #2060 | open | — | P3 | 2026-06-26 |
| #2061 | open | — | P4 | 2026-06-26 |
| #2062 | open | — | P5 | 2026-06-26 |
| #2063 | open | — | P7 | 2026-06-26 |
| #2064 | open | — | P6 | 2026-06-26 |

### Decisions
- 2026-06-26: engine-owned parser (`IREngine::args()` + `init(argc,argv)`) is
  the chosen mechanism for "no-arg targets get IRArgs free" (human ask).
- 2026-06-26: tools (P7) ARE in scope (human), requiring positional-arg +
  no-common-args framework extensions.

### Events
- 2026-06-26: filed via file-epic
