# engine/prefabs/irreden/common/ — position, identity, tags

Foundation layer every other domain builds on. Position, transform, name,
selection state, and the player tag. No simulation systems — those live
in `update/`.

## Key components

- `C_Position3D` — local vec3.
- `C_PositionGlobal3D` — world-space vec3. **Auto-added by `createEntity(...)`**.
- `C_PositionOffset3D` — ephemeral delta added on top of global.
  **Auto-added by `createEntity(...)`**.
- `C_Rotation` — Euler vec3.
- `C_Name` — debug/display string.
- `C_Player` — tag for player-controlled entities.
- `C_Selected` — tag for UI selection.
- `C_Modifiers` / `C_GlobalModifiers` / `C_NoGlobalModifiers` /
  `C_LambdaModifiers` / `C_ResolvedFields` — generic modifier
  framework. See [Modifier framework](#modifier-framework) below.

## Systems

None. Position math is read-only in `common/` — write paths live in
`update/systems/` (velocity, physics, animation).

## Modifier framework

`component_modifiers.hpp` declares the generic
`base + N modifications → effective` framework that generalizes the
position-offset / velocity-drag pattern. It is an engine-level
mechanism: any feature that wants a stack of additive / multiplicative
/ clamp / override modulations on a scalar field can register a
`FieldBindingId` and push `Modifier` records onto the entity's
`C_Modifiers` vector. The resolver pipeline composes them once per
UPDATE tick and writes the result to `C_ResolvedFields`.

The framework ships in two phases: type declarations (component types,
`Modifier` struct, static asserts) and the runtime (`FieldBindingId`
registry, the five resolver systems, source-destruction sweep,
`applyToField` query) in a follow-up.

Full design — locked choices, rationale, audit, public-API surface,
and decomposition — is in `docs/design/modifiers.md`. Read that
before adding to the framework or migrating an existing
`base + offset` pattern onto it.

Key invariants the design rests on:

- `Modifier` stays **trivially-copyable**. Anything needing inline
  `std::function` or `std::string` belongs in `C_LambdaModifiers`,
  not `C_Modifiers`.
- Public API lives in the `IRPrefab::Modifier::` namespace per the
  prefab-layer principle in `engine/prefabs/irreden/render/CLAUDE.md`,
  NOT in `IRRender::` or any engine-library-level namespace.
- Globals + exemption are dispatched via **archetype routing**
  (separate include / exclude resolver systems on
  `C_NoGlobalModifiers`), never via per-entity branching inside a
  tick body.
- Decay is built-in only as `ticksRemaining_` (an `int32_t` counter
  with `-1` as the sentinel for "no decay"). Curved / source-driven
  decay is the source entity's job, not the modifier struct's.

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
