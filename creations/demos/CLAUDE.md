# creations/demos/ — shareable example creations

Small, self-contained creations that exist to exercise or demonstrate a
specific engine feature. Each is an independent executable. These are the
only creations checked into the engine repo — private creations (games,
personal editors, experiments) live as gitignored subdirectories under
`creations/`.

## Current demos

- `default/` — minimal example showing how to spin up a `World`, register
  a component pack, and run a Lua script.
- `shape_debug/` — stress test for the shape renderer and voxel pipeline
  at varying subdivisions / zoom. Useful for chasing rendering glitches.
- `midi_keyboard/` — keyboard-driven MIDI input demo.
- `midi_polyrhythm/` — Lua-driven polyrhythm sequencer. Reference for
  how `lua_component_pack.hpp` + `main_lua.cpp` are wired up.
- `metal_clear_test/` — Metal-backend smoke test (clear-color only).

## Adding a new demo

1. Copy the closest matching existing demo (Lua-driven →
   `midi_polyrhythm`, C++-only → `shape_debug`, minimal → `default`).
2. Rename the target (`IRShapeDebug` → `IRYourName`) and update
   `CMakeLists.txt`:
   - `set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})`
   - `add_executable(IRYourName main.cpp)` +
     `target_link_libraries(IRYourName PUBLIC IrredenEngine)`
   - The Windows DLL POST_BUILD step.
   - Copy `engine/render/data`, `engine/data`,
     `engine/render/src/shaders` to the runtime dir.
   - An `IRYourNameRun` custom target with `WORKING_DIRECTORY
     ${runtime_dir}` and `USES_TERMINAL`.
3. Add `add_subdirectory(your_name)` to `creations/demos/CMakeLists.txt`.
4. Build with `cmake --build build --target IRYourName`.
5. Run with `IRYourNameRun` from VSCode, or manually with the PATH fix
   from the top-level `CLAUDE.md`.

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
- **Keep Lua bindings lean.** A demo's `lua_component_pack.hpp` should
  register **only** the components the demo actually uses. Don't copy
  the kitchen-sink pack from another demo just to save typing.
