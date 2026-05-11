# creations/demos/ — shareable example creations

Small, self-contained creations that exist to exercise or demonstrate a
specific engine feature. Each is an independent executable. These are the
only creations checked into the engine repo — private creations (games,
personal editors, experiments) live as gitignored subdirectories under
`creations/`.

## Inherits from engine baseline

Applies the rules in [`docs/agents/CLAUDE-BASELINE.md`](../../docs/agents/CLAUDE-BASELINE.md).
No opt-outs.

## Current demos

- `default/` — minimal example showing how to spin up a `World`, register
  a component pack, and run a Lua script.
- `shape_debug/` — stress test for the shape renderer and voxel pipeline
  at varying subdivisions / zoom. Useful for chasing rendering glitches.
- `midi_keyboard/` — keyboard-driven MIDI input demo.
- `metal_clear_test/` — Metal-backend smoke test (clear-color only).
- `lighting/` — lighting pipeline showcase (emissive voxels, light volume).
- `modifier_demo/` — interactive modifier framework showcase. Eight cubes
  each demonstrate one framework capability (Haste, Stun, Slow, Stack,
  GlobalSlow, LambdaSine, SourceKill, Clamp) via number keys 1–8. Live
  per-cube resolved-speed HUD. Canonical visual reference for the modifier
  framework — see `engine/prefabs/irreden/common/CLAUDE.md`.
- `sprite_demo/` — exercises `C_Sprite` / `C_SpriteAnimation` Lua bindings.
  Three sprites demonstrate LOOP, ONCE, and PING_PONG loop modes against a
  32×32 test sheet (4 × 16px cells: red, green, blue, yellow).
- `lua_pipeline_demo/` — minimal-render demo whose entire `initSystems`
  lives in `scripts/main.lua`. Exercises `IRSystem.registerPipeline`,
  `IRSystem.systemId(SystemName.X)`, and a Lua-defined system mixed
  into the UPDATE list alongside prefab systems and the modifier
  resolver chain. Reference for the T-102 Lua-driven-ECS pattern.
- `z_yaw_rotation/` — Z-axis world-rotation showcase. Two executables:
  `IRZYawStatic` (four SDF/voxel shapes auto-rotating in a ring, good
  as a visual-regression canary for `SCREEN_SPACE_RESIDUAL_ROTATE`) and
  `IRZYawInteractive` (same scene with `R`-key-toggled mouse-driven yaw
  and left-click per-voxel entity-pick via `getEntityIdAtMouseTrixel`).

## Adding a new demo

1. Copy the closest matching existing demo (Lua-driven →
   `default`, C++-only → `shape_debug`, minimal → `default`).
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
   from [`docs/agents/BUILD.md`](../../docs/agents/BUILD.md).

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
