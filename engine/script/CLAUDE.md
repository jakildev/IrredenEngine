# engine/script/ — Lua 5.4 via sol2

Thin wrapper around sol2 that exposes ECS components, math, and input to
Lua. Creations register *which* components get bound via a per-creation
`lua_component_pack.hpp` file.

## `LuaScript`

One `sol::state` per `World`. Opens `base`, `package`, `string`,
`table`, `math` libs and pre-binds an `IRMath` Lua table with:

- `fract`, `clamp01`, `lerp`, `lerpByte`.
- `hsvToRgb`.
- Layout helpers (`layoutGridCentered`, etc.).

Public API:

- `lua()` — raw `sol::state&` escape hatch.
- `scriptFile(const char* path)` — execute a Lua file via
  `sol::state::script_file`.
- `getTable(name)` — fetch a global table.
- `registerType<T, Ctors...>(name, kv_pairs...)` — raw sol2 usertype.
- `registerTypeFromTraits<T>()` — compile-time-checked usertype, requires
  `kHasLuaBinding<T> = true` and a `bindLuaType<T>(LuaScript&)` definition.
- `registerTypesFromTraits<Ts...>()` — batch.

## The binding-trait pattern

The split between engine types and creation-specific Lua exposure is
mediated by one trait in `script/lua_binding_traits.hpp`:

```cpp
template <typename T>
inline constexpr bool kHasLuaBinding = false;
```

Every component that wants a Lua binding has a sibling
`component_<name>_lua.hpp` file that:

1. `#include`s the main component header.
2. Specializes `kHasLuaBinding<C_Foo> = true`.
3. Defines `template <> void bindLuaType<C_Foo>(LuaScript&)` that calls
   `script.registerType<C_Foo, ...>(name, "x", &C_Foo::x_, ...)`.

The `*_lua.hpp` files exist across `engine/prefabs/irreden/**/components/`
— currently ~18 of them. A creation picks which ones to actually compile
and register via its own `lua_component_pack.hpp`:

```cpp
// creations/demos/default/lua_component_pack.hpp
#include <irreden/prefabs/common/components/component_position_3d_lua.hpp>
#include <irreden/prefabs/render/components/component_zoom_level_lua.hpp>
// ... only what this creation needs ...

void registerLuaComponentPack(LuaScript& script) {
    script.registerTypesFromTraits<C_Position3D, C_ZoomLevel /* ... */>();
}
```

This is the **only** way to add Lua bindings. If a creation forgets to
include the `_lua.hpp` header or omits the type from
`registerTypesFromTraits<>`, Lua sees nothing.

## Lua-defined components (`IRComponent.register`)

Lua scripts can declare new component types and attach them to entities,
with native SoA storage parallel to C++-typed components. Call
`LuaScript::bindLuaDrivenEcs()` once during creation init to expose the
surface, then in Lua:

```lua
local C_Hp = IRComponent.register("Hp", {
    current = 100,           -- int32
    max     = { type = "float", default = 100 },  -- explicit type form
    payload = { type = "table", default = {} },   -- opaque-table opt-in
})

local entity = IREntity.create_some_entity()
IREntity.addLuaComponent(entity, C_Hp, { current = 50 })  -- optional overrides
local hp = IREntity.getLuaComponent(entity, C_Hp)         -- table snapshot
IREntity.removeLuaComponent(entity, C_Hp)
```

- **Type inference:** integer literal → `int32`; float literal → `float`;
  string → `std::string`; bool → `bool`; function → `sol::function`. The
  short form fails registration with a Lua error if a default value isn't
  natively classifiable; nested tables in the short form are intentionally
  rejected. Use the explicit `{ type = "...", default = ... }` form to
  disambiguate (`current = { type = "float", default = 100 }`) or to opt
  in to opaque table storage (`payload = { type = "table", default = {} }`).
- **Identity rule:** registering the same `name` twice raises a Lua error.
  C++ and Lua share one `ComponentId` space (`EntityManager::registerComponentDynamic`
  goes through the same component-id allocator).
- **Modifier-framework integration:** scalar fields (`int32`, `float`,
  `bool`) auto-register a field binding `("TypeName.fieldName", id)` into
  `IRPrefab::Modifier::detail::globalFieldRegistry()` at registration time.
  The returned handle exposes those ids via `C_Hp.fields.current.bindingId`
  so Lua scripts can pass them to `IRModifier.add` once the modifier
  bindings ship in T-102. String / function / table fields receive
  `kInvalidFieldId` (not modifier-targetable).
- **Storage:** a Lua-typed component is one `IComponentDataLuaTyped`
  impl with one native vector column per declared field
  (`std::vector<int32_t>`, `std::vector<float>`, etc., per field type).
  Reading or writing a field is a typed vector index — never a per-frame
  `sol::table` lookup.

The full design is in [`docs/design/lua-driven-ecs.md`](../../docs/design/lua-driven-ecs.md).

## Lua-defined systems (`IRSystem.registerSystem`)

`bindLuaDrivenEcs()` also exposes `IRSystem.registerSystem`. A Lua
system is a runtime-archetype-typed dispatch: include / exclude
component sets and a tick body, both resolved against the same
`ComponentId` space as C++ components.

```lua
local sysId = IRSystem.registerSystem({
    name = "MoveByVelocity",
    components = { "C_Position3D", "C_Velocity3D", C_Marker },  -- string OR handle
    excludes = { "C_NoMove" },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local pos = arch.C_Position3D:at(i)
            local vel = arch.C_Velocity3D:at(i)
            arch.C_Position3D:setAt(i, C_Position3D.new(pos.x + vel.x, pos.y, pos.z))
        end
    end,
})
```

- **Component name resolution.** Strings are resolved against the
  Lua-name registry populated by `LuaScript::registerType` (so any
  component included in the creation's `lua_component_pack` is
  matchable by its bound name) and against the dynamic component
  registry (Lua-defined components by user name). Tables holding a
  numeric `componentId` field — i.e. the handle returned by
  `IRComponent.register` — are accepted directly. Any other entry, or
  a string that doesn't resolve in either registry, raises a Lua-side
  error pointing at the `lua_component_pack`.
- **Archetype-batched dispatch.** The tick body fires once per
  matched archetype per pipeline tick — *not* once per entity. Per-
  entity work happens entirely inside Lua via the column views, so
  the C++/Lua boundary is one `sol::function` call per archetype +
  one sol2 method call per row access.
- **Entity ids.** `arch.entityAt(i)` reads the row's `EntityId`
  directly from the archetype node — no per-tick column copy. The
  tick body sees the entity layout from `arch.length` and a single
  closure for id lookup; archetype mutation during a tick still
  needs to go through the deferred ECS API, just like a C++ system.
- **Column views.**
  - `arch.<name>` for a C++-bound component is a `LuaCppColumnView`:
    `:at(i)` returns the row as a sol::reference to T (read mutable
    fields), `:setAt(i, T.new(...))` overwrites the row by value.
    `setAt` exists because most existing `*_lua.hpp` bindings expose
    fields as getter lambdas rather than `sol::property` pairs, so a
    full-row replacement is the universal write path.
  - `arch.<name>` for a Lua-defined component is a
    `LuaTypedColumnView`: `:getField(i, "name")` /
    `:setField(i, "name", v)` for typed per-field access, or
    `:getRow(i)` / `:setRow(i, table)` for whole-row read/write.
- **Return value.** A `SystemId` (`lua_Integer`). Holds for use with
  `IRSystem.registerPipeline` (lands in T-102).
- **No begin/end ticks yet.** Lua-side `beginTick` / `endTick`
  hooks are not exposed in T-101; add them when a use case needs
  frame-scoped setup or teardown.

T-100 landed components; T-101 (this) lands systems; pipelines,
hot-reload, and the parity demo follow in T-102..T-104.

## Script resolution

`scriptFile(path)` passes the path straight to sol2 / `dofile`. Behavior:

- Bare filename → resolved from cwd (which is the exe's runtime dir at
  launch time; see [`docs/agents/BUILD.md`](../../docs/agents/BUILD.md) "Running an executable").
- Path with a directory separator → resolved from cwd.
- Absolute path → used as-is.

There is no sandbox, no path-traversal check, and no archive support.
Creations ship `.lua` files in `creations/<name>/scripts/` and a top-level
`main.lua`.

## Gotchas

- **Trait missing → link error.** `registerTypeFromTraits<T>()`
  `static_assert`s on `kHasLuaBinding<T>`. Forgetting the `_lua.hpp`
  include gives a cryptic linker error, not a runtime failure.
- **Entity-creation helpers max 12 components.** The batch-create bindings
  are hardcoded up to 12 template args; larger bundles need a native
  helper or a restructure.
- **`LuaScript` lifetime is absolute.** Destroying the `sol::state`
  invalidates every bound object, `sol::protected_function` callback, and
  coroutine. Don't hold Lua handles across `World` shutdown.
- **Engine-wide traits, per-creation exposure.** Adding a `*_lua.hpp`
  header does *not* expose the component — every creation that wants it
  still has to include the header and add the type to its component
  pack.
