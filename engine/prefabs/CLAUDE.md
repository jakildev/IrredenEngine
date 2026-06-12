# engine/prefabs/

Header-only components, systems, commands, and entity builders grouped by
domain under `engine/prefabs/irreden/`. Everything here is compiled by whoever
includes it — there is no prefab .cpp. Keep headers lean; heavy logic belongs
in `engine/<module>/src/`.

## Hard rules (read before editing)

Three rules that get violated most often when adding prefab systems and
components. The full reasoning, allowlists, and canonical patterns live in
[`.claude/rules/cpp-math.md`](../../.claude/rules/cpp-math.md),
[`.claude/rules/cpp-systems.md`](../../.claude/rules/cpp-systems.md), and
[`.claude/rules/cpp-ecs.md`](../../.claude/rules/cpp-ecs.md) — those auto-load
when you open any C++ file. The capsules here are reminders.

- **Math primitives go through IRMath** — never `glm::*` or `std::sin/cos/sqrt/abs/min/max/clamp` outside `engine/math/`. See [`.claude/rules/cpp-math.md`](../../.claude/rules/cpp-math.md).
- **System state lives on `System<N>` or in `SystemParams`.** Never
  function-local `static` for mutable or system-owned state —
  `static constexpr` / `static const` for genuine compile-time
  constants is fine. Prefer the **member-on-`System<N>`** form via
  `IRSystem::registerSystem<N, Cs...>(name)` (params as fields,
  hooks as named member functions); the explicit `Params` +
  `setSystemParams` form remains supported as an escape hatch.
  Both forms have the same per-tick cost. Full pattern in
  [`engine/system/CLAUDE.md`](../system/CLAUDE.md) and
  [`.claude/rules/cpp-systems.md`](../../.claude/rules/cpp-systems.md).
- **No per-entity `getComponent` inside a system tick** — see [`.claude/rules/cpp-ecs.md`](../../.claude/rules/cpp-ecs.md).

## Layout

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

Exceptions exist (e.g. `engine/prefabs/irreden/spatial/spatial_query.hpp`
lives directly in the domain root because it's a set of free functions, not a
component/system/command). When in doubt, follow the shape of existing files
in that domain.

## Conventions

- **Components** are plain data structs in `namespace IRComponents`, `C_`
  prefix, public members with trailing `_`. Helper methods are constrained
  by the categorization below.
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

## Component method rules

Component methods fall into three tiers:

**(a) Pure data.** Fields and a constructor that initializes them. Most
components in `common/`, `input/`, and tag-style components fall here.
(`common/` houses the canonical `C_LocalTransform` / `C_WorldTransform`
SQT pair; the legacy `C_Position3D` / `C_PositionGlobal3D` / `C_Rotation`
trio was retired in T-302.)

**(b) Self-only helpers (allowed).** Methods that read or write only the
component's own fields (and stack-locals derived from them). Examples:
`C_PeriodicIdle::tick()` advances its own angle, `C_CanvasFogOfWar::setCell`
writes its own grid, `toGPUFormat()` converts own fields into a render
struct.

**(c) Cross-entity reaches (forbidden).** Methods that look up *another*
entity through a stored `EntityId` field — `IREntity::getComponent`,
`setComponent`, `createEntity`, `setParent`, `getEntity`. These belong in
a system (per-frame work) or one of:

- **Entity builder** — `template <> struct IREntity::Prefab<NAME>` with a
  `create()` that wires up the entity bundle. Example:
  `engine/prefabs/irreden/render/entities/entity_trixel_canvas.hpp`.
- **Prefab-scoped namespace** — a sibling `<feature>.hpp` exposing a
  `namespace IRPrefab::Foo` with free functions. The namespace owns the
  entity-lookup logic; callers don't carry the entity id. Example:
  `engine/prefabs/irreden/render/fog_of_war.hpp`. Use this when an API
  needs an ergonomic free-function shape and the caller is unlikely to
  hold the entity id.

The split keeps component layout trivial (good for archetype iteration)
and concentrates per-entity orchestration where it belongs.

### Documented exceptions

These patterns *look* like (c) but are accepted because the alternatives
are worse:

- **GPU / IO resource RAII.** A constructor that calls
  `IRRender::createResource<>` and a destructor that calls
  `destroyResource<>` is fine — the component IS the resource owner, and
  splitting allocation into a system makes the lifetime contract harder
  to enforce. Examples: `C_TriangleCanvasTextures`, `C_TrixelFramebuffer`,
  `C_CanvasFogOfWar`, `C_CanvasSunShadow`, `C_CanvasAOTexture`,
  `C_CanvasLightVolume`.
- **`onDestroy()` IO cleanup.** A component that hooks the entity's
  destroy event to flush an external side effect (e.g.,
  `C_MidiNote::onDestroy()` sending NOTE_OFF) is fine when the cleanup
  must be synchronized with entity death and a per-component hook is
  simpler than a "scan dying entities" system. Document the side effect
  in the domain `CLAUDE.md`.
- **Constructor snapshots ambient state.** A constructor that reads
  `IRRender::getActiveCanvasEntity()` (or similar) to bind the component
  to the currently-active canvas is fine — no other entity is mutated,
  and the alternative is forcing every caller to thread the canvas id.
  Examples: `C_VoxelSetNew`, `C_ShapeDescriptor`.

Anything outside (a)/(b) and not on the exceptions list is a (c) violation
and should be moved to a system, builder, or namespace.

## Anti-patterns

ECS-general anti-patterns (getComponent in ticks, hot-path allocations, dirty flags, function-local static) live in [`.claude/rules/cpp-ecs.md`](../../.claude/rules/cpp-ecs.md) and [`.claude/rules/cpp-systems.md`](../../.claude/rules/cpp-systems.md). Prefab-specific anti-patterns:

- ❌ Adding a new system without updating the `SystemName` enum.
- ❌ Storing references to other entities' component storage across frames — archetype changes invalidate addresses.
- ❌ Cross-domain includes inside a prefab header (e.g. `voxel/` component including `audio/` component). Prefabs are grouped by domain on purpose; cross-domain composition belongs in a creation.
