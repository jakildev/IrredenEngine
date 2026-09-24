# engine/script/ — LuaJIT 2.1 via sol2

Rationale: [`script-lua-binding-surface.md`](../../docs/design/script-lua-binding-surface.md)
and [`lua-driven-ecs.md`](../../docs/design/lua-driven-ecs.md). Signatures live
in `lua_*_bindings.hpp`.

## Lua runtime: LuaJIT 2.1

Lua 5.1 plus `bit` and `ffi`. No `goto`, `<const>`/`<close>`, `bit32` (use
`bit`), `math.type`, or integer subtype: a whole-number float default
(`x = 0.0`) infers as `int32`, so write `x = { type = "float", default = 0 }`.
Keep `SOL_EXCEPTIONS_ALWAYS_UNSAFE` defined on `IrredenEngineScripting`, or a
C++ exception reaches Lua as a bare `"C++ exception"`.

## The binding-trait pattern

A C++ component is Lua-visible only through a sibling `component_<name>_lua.hpp`
specializing `kHasLuaBinding<C_Foo>` and defining `bindLuaType<C_Foo>`. A
creation lists its types in `lua_component_pack.hpp`; unlisted types are hidden.

- The `registerType` name is the literal class name and the `IRComponent.C_Foo`
  handle key. Call `bindLuaDrivenEcs()` first, or the handle is never written.
- **C++-component field writes:** bind scalars as member pointers and math fields
  as `sol::property` over `{x,y,z[,w]}` via `*FromLua`. `C_LocalTransform` is the
  reference; `setAt(i, T.new(...))` writes whole rows.

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
- In a system tick use `deferredCreate` / `deferredDestroy`; use
  `deferredCall(fn)` for builders or rebuilds. It runs at the next structural
  flush on the main thread; callback errors are logged, not raised.

### Packed vec3 / ivec3 / vec4 fields

Reads allocate `{x,y,z[,w]}` tables; writes take a table or IRMath userdata.
`vec4` defaults to identity and is EVAL-only; `vec2` is unsupported. CODEGEN
reads `.x/.y/.z` and writes `vec3.new(...)`/`ivec3.new(...)`.

### Two-tier accessor contract

`addLuaComponent(e, C, overrides)` and `getLuaComponent` build string-keyed
tables: setup and inspection only. Per tick, resolve `C.fields.f.index` once and
call `getLuaField(e, C, idx)` / `setLuaField(e, C, idx, v)`.

### Which view do I get, and what can I call on it?

| Declared as | Tick column view | `*LuaField`, `getLuaComponent` | Attach from Lua | `bindingId` |
|---|---|---|---|---|
| Codegen'd (`register` in an `irreden_lua_codegen` source) | `LuaCppColumnView`: `:at(i)`, `:setAt(i, C.new(...))` | raise | ✓ | ✗ |
| Lua-typed (`register` at runtime) | `LuaTypedColumnView`: `:getField`/`:setField(i, "f")`, `:getRow`/`:setRow` | ✓ | ✓ | scalars |
| Hand-written `*_lua.hpp` | `LuaCppColumnView` | raise | via `registerComponentAttachFactory` | ✗ |

`removeLuaComponent`, `hasLuaComponent`, `IREntity.singleton` work for all three.
A `:setField` tick body over codegen'd components cannot run under EVAL: pin it
with `mode = "codegen"`.

## Lua-defined enums (`IREnum.register`)

`IREnum.register("DeviceType", { "EFFECT", "SYNTH" })` returns a 0-based table
at `IREnum.DeviceType`. Invalid/duplicate input raises; a typo reads `nil`
([`cpp-lua-enums.md`](../../.claude/rules/cpp-lua-enums.md)). CODEGEN ticks
cannot use members.

## Build-time codegen of Lua-defined components (CODEGEN mode)

`irreden_lua_codegen(...)` emits each component and its binding, plus
`registerCodegenComponents`, `registerCodegenSystems`, and `kDefaultEcsMode`.

- **Field types:** `int32`, `float`, `bool`, `string`, `vec3`, `ivec3`; others
  are codegen-time errors. **Field order is alphabetical** (struct and `C.new`).
- The registry is `IRScript::CodegenRegistry::<run id>` (the output stem by
  default). Qualify when including two runs; override invalid/duplicate ids.
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

`registerSystem({ name, components, excludes?, tick, concurrency?, mode? })`
returns a `SystemId` for any pipeline.

- Name components by handle (`IRComponent.C_LocalTransform`, or the value
  `IRComponent.register` returned); strings resolve but hide typos.
- `tick` runs once per matched archetype over `arch.length`, `arch.entityAt(i)`,
  and the column views; structural changes use `IREntity.deferred*`.
- `concurrency` takes `IRSystem.Concurrency.*`; EVAL runs `PARALLEL_FOR` as
  `MAIN_THREAD`. No begin/end ticks yet.
- `IRSystem.replaceSystemBody(id, fn)` swaps an EVAL Lua system's tick; other
  ids raise. The component filter is fixed at registration.

## Pipeline composition (`IRSystem.registerPipeline`, `IRSystem.SystemName`)

C++ declares nameable prefab systems with `registerPrefabSystems` or
`registerPrefabSystemId`; Lua spells `IRTime.X` and
`IRSystem.systemId(IRSystem.SystemName.X)`, which raises for an unknown name.

- `registerPipeline` / `registerPipelineGroups` **replace** the event's list;
  extend a C++ list with `appendSystem` / `insertSystemBefore/After`
  ([pipeline docs](../system/CLAUDE.md#live-pipeline-composition)).
- Throttled systems use `setSystemCadence`, `getAccumulatedTicks`, and
  `accumulatedDeltaTime`.
- Bind names/events with `IR_BIND_SYS` / `IR_BIND_TIME`.

## Engine service bindings

### IRFog

`LuaScript::bindLuaFog()` installs the opt-in engine table, preserves custom
keys, and stays separate from `bindLuaDrivenEcs()`.

- `setVision` replaces circles; `addVision` appends; `clearVisions` clears only
  circles. Their optional defaults are `edge = kFogVisionEdgeDefault`,
  `observerZ = zCostUp = freeBand = 0`, and `zCostDown = -1` (mirror up-cost).
- `evalReveal(x,y,z)` is the BODY verdict oracle: a VISIBLE grid cell or the
  circle term, never the hysteretic body state. Attached fog with nothing
  revealed returns 0; absent fog returns 1. `lineOfSight` always returns true.
- `setEntityGoverned(id, governed?)` defaults true (synchronous BODY); `false`
  tags FIELD, even on a never-adopted entity. It changes archetypes, so defer it
  during iteration. `getEntityReveal` is stored body reveal, or 1 when untagged.
- `setCell`, `getCell`, and `revealRadius` edit/query the grid; `clear()` clears
  only that grid. States are `UNEXPLORED`, `EXPLORED`, and `VISIBLE`.

The tested examples are
[`fog_binding_selftest.lua`](../../creations/demos/fog_demo/scripts/fog_binding_selftest.lua)
and its [cap/governance companion](../../creations/demos/fog_demo/scripts/fog_binding_cap_selftest.lua).
These are setup/EVAL APIs, not tick intrinsics. Follow-ups are **IRFog occlusion
integration** and **IRFog subject-model and channel integration**.

- **`IRModifier`:** `add*` writes `C_Modifiers`; resolved values need
  `registerResolverPipeline()` in UPDATE. Wrong types no-op; cache ids hot.
- **`IRCollision.onOverlap*`:** raises unless `DISPATCH_LUA_OVERLAP` follows
  `COLLISION_NOTE_PLATFORM` in UPDATE.
- **`IRPersist.saveWorld/loadWorld`:** only at a frame boundary, never in a tick
  or callback; call `IRWorld.resetGameplay()` immediately before loading.
- **`IRGui.draw*` / `IRDebug.draw*`:** immediate; re-issue in RENDER after
  `TEXT_TO_TRIXEL` / before `DEBUG_OVERLAY` respectively.
- **Widgets:** Lua `onClick` raises unless `WIDGET_LUA_DISPATCH` follows
  `WIDGET_INPUT` in INPUT.

## Commands and input (`IRCommand.*`, `IRInput.*`)

`LuaScript::bindLuaCommands()`; design in
[`docs/design/lua-input-commands.md`](../../docs/design/lua-input-commands.md).
Compose modifiers with `bit.bor`. A `createCommand` body appears in the F1
overlay only with `name`/`description`; `isButtonBound` is modifier-blind. A
new prefab command needs an `IR_BIND_CMD` line in `lua_command_bindings.hpp`
and a case in `ir_command.cpp`'s `fireByName`/`bindPrefabCommand`.

## Prefab format (`Prefab.register`, `Prefab.spawn`)

A prefab has a version plus optional refs, rotation, canvas, components, and
setup. `spawn(id,pos)` returns a `LuaEntity` or `nil,err`. Detached rotations
need `canvas_size`; declarative components need `registerComponentFactoryFor`.
`bindPoint` is spawn-time only. The registry is process-global; tests clear it.

## Script output

`LuaScript` binds `print` to `ScriptLog`: one flushed, timestamped line per call,
arguments tab-joined verbatim and ordered with engine/client logs. Flushing is
load-bearing under redirected stdout and signal death. `io.write` and a bare
`sol::state` keep stock buffered behavior.

## Script resolution

`scriptFile(path)` passes the path to sol2 unchanged; relative paths resolve from
cwd ([`BUILD.md`](../../docs/agents/BUILD.md) §"Running an executable").

## Gotchas

- `registerTypeFromTraits<T>()` without its `_lua.hpp` include is a link error.
- `IRScript::*FromLua` (`ir_script_utils.hpp`) default instead of raising, so
  check the type first; `sol::object::is<sol::table>()` is true for userdata.
- Batch-create factories must match C++ arity
  ([`cpp-ecs-smells.md`](../../.claude/rules/cpp-ecs-smells.md)).
- `LuaScript` lifetime is absolute: its destruction invalidates every Lua handle.
