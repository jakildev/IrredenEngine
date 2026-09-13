# engine/script/ — LuaJIT 2.1 via sol2

Rationale for these rules: [`docs/design/script-lua-binding-surface.md`](../../docs/design/script-lua-binding-surface.md);
ECS design: [`docs/design/lua-driven-ecs.md`](../../docs/design/lua-driven-ecs.md).
Signatures live in `include/irreden/script/lua_*_bindings.hpp`.

## Lua runtime: LuaJIT 2.1

Lua 5.1 plus `bit` and `ffi`. No `goto`, `<const>`/`<close>`, `bit32` (use
`bit`), `math.type`, or integer subtype: a whole-number float default
(`x = 0.0`) infers as `int32`, so write `x = { type = "float", default = 0 }`.
Keep `SOL_EXCEPTIONS_ALWAYS_UNSAFE` defined on `IrredenEngineScripting`;
without it a C++ exception reaches Lua as a bare `"C++ exception"`.

## The binding-trait pattern

A C++ component is Lua-visible only through a sibling
`component_<name>_lua.hpp` that specializes `kHasLuaBinding<C_Foo> = true` and
defines `bindLuaType<C_Foo>(LuaScript&)` calling
`script.registerType<C_Foo, ...>("C_Foo", ...)`. A creation includes the headers
it wants and lists the types in its `lua_component_pack.hpp`
(`registerTypesFromTraits<...>()`); an unlisted type is invisible.

- The `registerType` name is the literal class name and the
  `IRComponent.C_Foo` handle key. Call `bindLuaDrivenEcs()` first, or the
  handle is never written.
- **C++-component per-field writes from Lua:** bind a scalar as a member
  pointer; bind an `IRMath::vec3`/`vec4`/`Color` field as
  `sol::property(getter, setter)` over `{x, y, z[, w]}` tables via the
  `*FromLua` helpers (a bare math member pointer is unusable from Lua).
  `C_LocalTransform` is the reference; `setAt(i, T.new(...))` writes whole rows.

## Lua-defined components (`IRComponent.register`)

`IRComponent.register("Hp", { current = 100, max = { type = "float", default = 100 } })`
declares a component with native per-field columns in the C++ `ComponentId` space.

- **Type inference:** integer → `int32`, fractional → `float`, string, bool,
  function. Explicit tags: `int32`, `float`, `bool`, `string`, `table` (opaque
  opt-in), `vec3`, `ivec3`, `vec4` (`quat`/`quaternion` alias it). A nested
  short-form table or an unclassifiable default raises.
- A duplicate name raises unless `registerCodegenComponents` registered it, in
  which case the existing handle returns.
- Scalar `int32`/`float`/`bool` fields expose `C.fields.<f>.bindingId` for
  `IRModifier`; others get `kInvalidFieldId`.
- In a system tick use `IREntity.deferredCreate({ { C_Hp, overrides } })`
  (returns a reserved `EntityId`) and `IREntity.deferredDestroy(id)`.

### Packed vec3 / ivec3 / vec4 fields

Explicit tag plus a keyed or positional default. Reads return allocating
`{x, y, z[, w]}` tables (not for per-tick paths); writes take a table or the
IRMath userdata. The `vec4` default is identity `(0, 0, 0, 1)`. `vec4`/`quat`
are EVAL-only; `vec2` is unsupported. A CODEGEN tick reads `.x/.y/.z` and
writes `vec3.new(...)`/`ivec3.new(...)`.

### Two-tier accessor contract

`addLuaComponent(e, C, overrides)` and `getLuaComponent` build string-keyed
tables: setup and inspection only. Per tick, resolve `C.fields.f.index` once
and call `getLuaField(e, C, idx)` / `setLuaField(e, C, idx, v)`.

### Which view do I get, and what can I call on it?

| Declared as | Tick column view | `*LuaField`, `getLuaComponent` | Attach from Lua | `bindingId` |
|---|---|---|---|---|
| Codegen'd (`register` in an `irreden_lua_codegen` source) | `LuaCppColumnView`: `:at(i)`, `:setAt(i, C.new(...))` | raise | ✓ | ✗ |
| Lua-typed (`register` at runtime) | `LuaTypedColumnView`: `:getField`/`:setField(i, "f")`, `:getRow`/`:setRow` | ✓ | ✓ | scalars |
| Hand-written `*_lua.hpp` | `LuaCppColumnView` | raise | via `registerComponentAttachFactory` | ✗ |

`removeLuaComponent`, `hasLuaComponent`, `IREntity.singleton` work for all
three. A `:setField` tick body over codegen'd components cannot run under EVAL:
pin it with `mode = "codegen"`.

## Lua-defined enums (`IREnum.register`)

`IREnum.register("DeviceType", { "EFFECT", "SYNTH" })` returns a name → 0-based
ordinal table, also at `IREnum.DeviceType`. Bad, empty, or duplicate members, a
duplicate enum, and the name `"register"` raise; a typo'd member reads `nil`
([`cpp-lua-enums.md`](../../.claude/rules/cpp-lua-enums.md)). Ordinals match
under CODEGEN and EVAL, but a CODEGEN tick body cannot use a member.

## Build-time codegen of Lua-defined components (CODEGEN mode)

`irreden_lua_codegen(<target> SOURCES a.lua OUTPUT_HPP <hpp> [DEFAULT_MODE m] [REGISTRY_NAMESPACE id])`
emits an `IRComponents::C_Name` struct and binding per `IRComponent.register`,
plus `registerCodegenComponents`, `registerCodegenSystems`, and `kDefaultEcsMode`.

- **Field types:** `int32`, `float`, `bool`, `string`, `vec3`, `ivec3`; others
  are codegen-time errors. **Field order is alphabetical** (struct and `C.new`).
- The registry lives in `IRScript::CodegenRegistry::<run id>` (default: the
  `OUTPUT_HPP` stem), re-exported unqualified. A TU including two runs
  qualifies with the id; a duplicate or keyword id needs `REGISTRY_NAMESPACE`.
- A `duplicate symbol` on `C_Foo_declared_by_more_than_one_codegen_run_in_this_binary`
  means two runs declare `C_Foo`. Headers may be included from any number of TUs.

### CODEGEN system bodies

`IRSystem.registerSystem` in a codegen source lowers to a typed per-row lambda.
Anything outside this DSL is a file:line build error:

- One top-level `for i = 0, arch.length - 1 do ... end`; no other loop or jump.
- `:at`/`:setAt`/`:getField`/`:setField` (literal names) on Lua-defined
  components of the same run; `C.new(...)`; arithmetic except `^`;
  comparisons; `and`/`or`/`not`; `if`; single-target `local`.
- `kIntrinsicRegistry` (`cmake/lua_codegen/system_dsl.cpp`) intrinsics:
  value-returning (`math.*` → `IRMath::*`) inside expressions only;
  `isStatement_` setters (`IRRender.setSunIntensity`) as statements only.
- No C++-bound component types, upvalues, metatables, dynamic dispatch,
  `require`, varargs, `nil`, `..`/`string.format`; use `mode = "eval"`.

### Per-system mode override + CODEGEN/EVAL coexistence

`mode = "eval"`/`"codegen"` overrides the default; other values raise. Build
default: `DEFAULT_MODE`, else `-DIR_LUA_ECS_DEFAULT_MODE` (`CODEGEN`). Runtime
default: `LuaScript::setEcsDefaultMode` (`EVAL`). With codegen, initialize in
order — `bindLuaDrivenEcs()`, `registerCodegenComponents(lua)`,
`setEcsDefaultMode(kDefaultEcsMode)`, `registerCodegenSystems()`, then
`scriptFile` — so codegen systems are not registered twice.

## Lua-defined systems (`IRSystem.registerSystem`)

`registerSystem({ name, components, excludes?, tick = function(arch) end, concurrency?, mode? })`
returns a `SystemId` for any pipeline.

- Name components by handle (`IRComponent.C_LocalTransform`, or the value
  `IRComponent.register` returned); strings resolve but hide typos.
- `tick` runs once per matched archetype over `arch.length`,
  `arch.entityAt(i)`, and the column views; structural changes use
  `IREntity.deferred*`.
- `concurrency` takes `IRSystem.Concurrency.*`; EVAL runs `PARALLEL_FOR` as
  `MAIN_THREAD`. No begin/end ticks yet for Lua systems.
- `IRSystem.replaceSystemBody(id, fn)` swaps an EVAL Lua system's tick; other
  ids raise. The component filter is fixed at registration.

## Pipeline composition (`IRSystem.registerPipeline`, `IRSystem.SystemName`)

C++ declares nameable prefab systems with
`script.registerPrefabSystems<IRSystem::LIFETIME, ...>()` or
`registerPrefabSystemId(name, id)`; Lua spells `IRTime.X` and
`IRSystem.systemId(IRSystem.SystemName.X)`, which raises for an unregistered name.

- `registerPipeline` / `registerPipelineGroups` **replace** the event's list;
  add to a C++-built one with `appendSystem` / `insertSystemBefore/After`
  ([`engine/system/CLAUDE.md`](../system/CLAUDE.md) §"Appending to a live pipeline").
- A throttled system (`setSystemCadence`) integrates with
  `getAccumulatedTicks` / `accumulatedDeltaTime`.
- Add or remove a `SystemName` / time event with an `IR_BIND_SYS` line in
  `lua_pipeline_bindings.hpp` / an `IR_BIND_TIME` line in `bindIRTimeEvents`.

## Engine service bindings

- **`IRModifier`:** `add*` writes only `C_Modifiers`; resolved values need the
  `registerResolverPipeline()` systems in UPDATE. A wrong-typed push silently
  no-ops. Cache a `FieldBindingId` instead of a name on hot paths.
- **`IRCollision.onOverlap*`** raises unless `DISPATCH_LUA_OVERLAP` is
  registered and placed after `COLLISION_NOTE_PLATFORM` in UPDATE.
- **`IRPersist.saveWorld/loadWorld`:** frame boundary only, never inside a tick
  or callback; call `IRWorld.resetGameplay()` right before `loadWorld`.
- **`IRGui.draw*`** (0-255 colors) and **`IRDebug.draw*`** (0..1, unchecked)
  are immediate mode: re-issue every frame from a RENDER system, after
  `TEXT_TO_TRIXEL` for `IRGui`, before `DEBUG_OVERLAY` for `IRDebug`.

### Widget framework bindings

A Lua `onClick` raises unless `registerPrefabSystem<IRSystem::WIDGET_LUA_DISPATCH>()`
is registered and that id sits in INPUT immediately after `WIDGET_INPUT`.

## Commands and input (`IRCommand.*`, `IRInput.*`)

`LuaScript::bindLuaCommands()`; design in
[`docs/design/lua-input-commands.md`](../../docs/design/lua-input-commands.md).
Compose modifiers with `bit.bor`. A `createCommand` body appears in the F1
overlay only with `name`/`description`. `isButtonBound` is modifier-blind. A
new prefab command needs an `IR_BIND_CMD` line in `lua_command_bindings.hpp`
and a case in `ir_command.cpp`'s `fireByName`/`bindPrefabCommand`.

## Prefab format (`Prefab.register`, `Prefab.spawn`)

A prefab returns `{ prefab_version = 1, voxel_ref?, rig_ref?, rotation_mode?, canvas_size?, components?, setup?, ... }`.
`Prefab.spawn(id, pos)` returns a `LuaEntity` or `nil, err`, leaving no entity.
`rotation_mode` takes `IRComponent.RotationMode.*`; detached modes need
`canvas_size`. Declarative components (`components = { C_ZoomLevel = {...} }`)
need `registerComponentFactoryFor<C>` and run before `setup`. `IREntity.bindPoint`
is a spawn-time query, not per-tick. The registry is process-global: tests call
`clearPrefabs()`.

## Script resolution

`scriptFile(path)` passes the path to sol2 unchanged; relative paths resolve
from cwd ([`BUILD.md`](../../docs/agents/BUILD.md) §"Running an executable").

## Gotchas

- `registerTypeFromTraits<T>()` without its `_lua.hpp` include is a link error.
- `IRScript::*FromLua` (`ir_script_utils.hpp`) default instead of raising, so
  check the type first; `sol::object::is<sol::table>()` is true for userdata.
- Batch-create factories must match the C++ arity ([`cpp-ecs-smells.md`](../../.claude/rules/cpp-ecs-smells.md)).
- `LuaScript` lifetime is absolute: its destruction invalidates every Lua handle.
