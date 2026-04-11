# engine/prefabs/

Header-only components, systems, commands, and entity builders grouped by
domain under `engine/prefabs/irreden/`. Everything here is compiled by whoever
includes it — there is no prefab .cpp. Keep headers lean; heavy logic belongs
in `engine/<module>/src/`.

## Layout

```
engine/prefabs/irreden/
  common/   — position, transform, name, player, tags, selection
  update/   — physics, movement, collision, animation, particles
  voxel/    — voxel pools, voxel sets, sculpting, shape descriptors
  input/    — key/mouse/gamepad input components, hover detect, hitboxes
  render/   — trixel canvases, framebuffers, cameras, text
  audio/    — MIDI messages, sequences, channels, devices
  video/    — screenshot + recording commands
  asset/    — asset-related prefabs (currently small)
  demo/     — demo-specific scratch prefabs (do not ship into creations)
  wip/      — experimental prefabs not ready for production
```

Each domain has its own `CLAUDE.md` with the components/systems/commands
catalog and the patterns specific to that domain. Read the nearest one
before adding or modifying.

## File pattern

| Type        | Path                                         | Example                      |
|-------------|----------------------------------------------|------------------------------|
| Component   | `<domain>/components/component_<name>.hpp`   | `component_move_order.hpp`   |
| Lua binding | `<domain>/components/component_<name>_lua.hpp` | `component_move_order_lua.hpp` |
| System      | `<domain>/systems/system_<name>.hpp`         | `system_velocity_drag.hpp`   |
| Command     | `<domain>/commands/command_<name>.hpp`       | `command_zoom_in.hpp`        |
| Entity      | `<domain>/entities/entity_<name>.hpp`        | `entity_camera.hpp`          |

Exceptions exist (e.g. `engine/prefabs/irreden/update/nav_query.hpp` lives
directly in the domain root because it's a set of free functions, not a
component/system/command). When in doubt, follow the shape of existing files
in that domain.

## Conventions

- **Components** are plain data structs in `namespace IRComponents`, `C_`
  prefix, public members with trailing `_`. No methods on components except
  simple constructors and helpers.
- **Systems** use `template <> struct IRSystem::System<SYSTEM_NAME>` with a
  static `create()` returning `SystemId`. The `SYSTEM_NAME` must exist in
  `engine/system/include/irreden/ir_system_types.hpp` (`SystemName` enum).
- **Commands** use `template <> struct IRCommand::Command<COMMAND_NAME>`
  with a static `create()` returning a typed callable. `COMMAND_NAME` must
  exist in the corresponding command enum.
- **Entities** are prefab constructor helpers — free functions in
  `IREntity::` or similar that call `createEntity(...)` with a canned
  component bundle.
- **Lua variants** (`*_lua.hpp`) specialize `kHasLuaBinding<T> = true` and
  implement `bindLuaType<T>(LuaScript&)`. They are only included when a
  creation's `lua_component_pack.hpp` lists them.

## Anti-patterns

- ❌ Per-entity `getComponent` inside a system tick function. Fix: add the
  component to the system's template parameters.
- ❌ Allocating memory in a hot tick path (`std::vector` push, string
  concat, `new`). Reserve once at `beginTick`.
- ❌ Adding a new system without updating the `SystemName` enum.
- ❌ Storing references to other entities' component storage across frames
  — archetype changes invalidate addresses.
- ❌ Cross-domain includes inside a prefab header (e.g. `voxel/` component
  including `audio/` component). Prefabs are grouped by domain on purpose;
  cross-domain composition belongs in a creation.
