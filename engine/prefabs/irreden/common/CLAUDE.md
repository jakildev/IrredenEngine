# engine/prefabs/irreden/common/ — position, identity, tags

Foundation layer every other domain builds on. Position, transform, name,
selection state, and the player tag. No simulation systems — those live
in `update/`.

## Key components

- `component_position_3d.hpp` — `C_Position3D`, local vec3.
- `component_position_global_3d.hpp` — `C_PositionGlobal3D`, world-space
  vec3. **Auto-added by `createEntity(...)`**.
- `component_position_offset_3d.hpp` — `C_PositionOffset3D`, ephemeral
  delta added on top of global. **Auto-added by `createEntity(...)`**.
- `component_rotation.hpp` — Euler vec3.
- `component_name.hpp` — debug/display string.
- `component_player.hpp` — tag for player-controlled entities.
- `component_selected.hpp` — tag for UI selection.
- `component_tags_all.hpp` — enum registry of tag components.

## Systems

None. Position math is read-only in `common/` — write paths live in
`update/systems/` (velocity, physics, animation).

## Commands

- `command_suite_camera.hpp` — registers `ZOOM_IN`, `ZOOM_OUT`,
  `MOVE_CAMERA_*`, `CLOSE_WINDOW`. A convenience bundle creations pick
  up en masse.

## Gotchas

- **`createEntity` always adds `C_PositionGlobal3D` + `C_PositionOffset3D`.**
  Rendered position is `global + offset`. Never infer a draw position
  from `C_Position3D` alone — it is the *local* position and only the
  `common/` hierarchy resolves it.
- **Don't duplicate position components.** Adding your own
  `C_PositionGlobal3D` second on top of the auto-added one leaves one
  column stale and causes jitter.
- **No systems means no ownership.** Any code is free to write to any
  position component here — that's the coordination-by-convention part.
  The `update/` domain's systems are the ones that write velocity-driven
  updates.
