# creations/demos/ — shareable example creations

Small, self-contained creations that exist to exercise or demonstrate a
specific engine feature. Each is an independent executable. These are the
only creations checked into the engine repo — private creations (games,
personal editors, experiments) live as gitignored subdirectories under
`creations/`.

## Inherits from engine baseline

Applies the rules in [`docs/agents/CLAUDE-BASELINE.md`](../../docs/agents/CLAUDE-BASELINE.md).
No opt-outs.

## Reference demos

Canonical starting points: `shape_debug` (C++-only reference and visual-regression
smoke target), `default` (minimal Lua-driven), `lua_perf_grid` (codegen toolchain
reference, T-106..T-108 pattern). Use `Glob creations/demos/*/` to see the full list.

## Adding a new demo

Copy the closest demo, rename targets, add `add_subdirectory(your_name)` to
`creations/demos/CMakeLists.txt`. See `shape_debug/CMakeLists.txt` for the
canonical CMake shape.

## Conventions

- **Self-contained.** A demo must build and run without any private
  creation being present. Don't cross-link from a demo to a private
  creation's code.
- **Small.** A demo is supposed to *demonstrate*, not be a full game.
  If a demo is growing its own architecture, split it into a private
  creation.
- **Deterministic where possible.** Avoid gratuitous randomness; makes
  the demo useful as a visual regression canary.
- **No runtime arguments.** Configuration lives in the creation's
  own config or a top-level `main.lua`. Command-line flags are fine
  for debug toggles but should never be required.

## Gotchas

- **Runtime DLL copying is not free.** Every demo that links against
  FFmpeg/sol2/GLFW gets a POST_BUILD copy. Large demos slow down their
  incremental builds. Keep demos minimal.
- **Don't edit `creations/demos/CMakeLists.txt` to re-order
  subdirectories.** Order doesn't affect build correctness, but some
  demos implicitly assume others have been built (tools/data pipelines
  especially). Append new entries; don't reshuffle.
- **Runtime directory layout is exe-relative.** `data/`, `shaders/`,
  `scripts/` must be siblings of the `.exe`. If you launch from the
  project root, file loading fails — use the `IR<Name>Run` target.
- **`IR*Run` must not repeat the asset-copy commands.** Asset staging is
  owned by `irreden_bundle_assets(<target> ...)` (parent
  [`creations/CLAUDE.md`](../CLAUDE.md) §"CMake boilerplate"), and the
  `IR*Run → IR*Exe → IR*Assets` dependency chain already guarantees
  `data/`/`shaders/`/`scripts/` are staged before the exe runs. Re-running
  `copy_directory` in the `IR*Run` target is redundant (idempotent but
  misleading — a reader of `Run` has no reason to suspect the `DEPENDS` chain
  covers it). A new demo copied from a reference must not carry the duplicate
  copies.
- **Keep Lua bindings lean.** A demo's `lua_component_pack.hpp` should
  register **only** the components the demo actually uses. Don't copy
  the kitchen-sink pack from another demo just to save typing.
