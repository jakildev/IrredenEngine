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
- **C++-component field writes:** bind scalars as member pointers; math fields
  (`{x,y,z[,w]}` via `*FromLua`, ref `C_LocalTransform`) and range-limited enums (a
  throwing setter, ref `C_VoxelSetNew.lodMin`) as `sol::property`. `setAt` writes rows.
- **Usertype ownership:** the engine registers `LuaEntity` (constructor) and
  `_IRLuaCppColumnView`/`_IRLuaTypedColumnView` (`bindLuaDrivenEcs`); never
  re-register them. A second `new_usertype<T>` (pinned sol2) strips `T`'s
  `__index` at the next GC: `registerType` warns and refuses; raw `new_usertype` won't.

## Lua-defined components (`IRComponent.register`)

`IRComponent.register("Hp", { current = 100, max = { type = "float", default = 100 } })`
declares a component with native per-field columns in the C++ `ComponentId` space.

- **Type inference:** integer → `int32`, fractional → `float`, string, bool,
  function. Explicit tags: `int32`, `float`, `bool`, `string`, `table` (opaque
  opt-in), `vec3`, `ivec3`, `vec4` (`quat`/`quaternion` alias it). A nested
  short-form table or an unclassifiable default raises.
- A duplicate name raises; a C++-bound one returns its handle unless
  `setCodegenCoexistence(false)`. `IRComponent.list()` lists the Lua-typed rest.
- Scalar `int32`/`float`/`bool` fields expose `C.fields.<f>.bindingId` for
  `IRModifier`; others get `kInvalidFieldId`.
- In a system tick use `deferredCreate`/`deferredDestroy`/`deferredDestroyTree`;
  wrap builders, rebuilds, and the `setParent`-family verbs in `deferredCall(fn)`,
  which runs at the next main-thread flush; callback errors are logged, not raised.

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
  value-returning (`math.*`, `IRRender.getActiveLodTier`) inside expressions only;
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

### IRAsset and voxel authoring

Registering the `C_VoxelSetNew` Lua trait installs
`IRAsset.loadVoxelSet(path[, anchor[, targetCanvas]])` and the integer-backed `IRShape`
table. Authoring contracts and methods live in
[`engine/prefabs/irreden/voxel/CLAUDE.md`](../prefabs/irreden/voxel/CLAUDE.md).
The loader returns a DENSE component value and defaults its anchor to `CENTER`;
voxel mutation is main-thread setup work, never `PARALLEL_FOR` tick work.

### IRFog

`LuaScript::bindLuaFog()` installs the opt-in table, keeping custom keys, apart from `bindLuaDrivenEcs()`.

- `setVision` replaces sources; `addVision` appends (past the analytic cap, an XY disc in the
  field's transient layer, admitted per cell mask); `clearVisions` clears both. Defaults: `edge =
  kFogVisionEdgeDefault`, `observerZ = zCostUp = freeBand = 0`, `zCostDown = -1` (mirror up-cost).
- `evalReveal(x,y,z)` is the BODY verdict oracle: a VISIBLE grid cell or LOS-gated circle term, never
  hysteretic body state; attached fog revealing nothing returns 0, absent fog 1.
  `lineOfSight` rebuilds per call; for many, capture once, then call `lineOfSightCaptured`.
  `setEntityGoverned(id, governed?)` defaults true (synchronous BODY); `false` tags FIELD; defer during iteration.
  `setHiddenPolicy(id, IRComponent.FogHiddenPolicy.*)` selects HIDE or GHOST; `hiddenPolicy(id)`
  reads it, and both reject non-BODY entities. `getEntityReveal` is stored reveal, or 1 when untagged.
- `setCell`, `getCell`, `revealRadius(cx,cy,r[,channels])`, `exploreRadius(...)`, `set/getCellChannels`
  edit/query the grid (`IRFog.State.*`, `IRFog.Channel.DEFAULT`); `clear()` clears only that grid.
  `setExploredPolicy(IRFog.ExploredPolicy.*, durationMs[, channels])` is init-only (refused once cells
  exist); `setExploredTimeMs` is the creation's monotonic decay clock; both raise named errors, state unchanged.

The [fog selftest](../../creations/demos/fog_demo/scripts/fog_binding_selftest.lua) and [cap/governance companion](../../creations/demos/fog_demo/scripts/fog_binding_cap_selftest.lua) cover these setup/EVAL APIs. `setVisionLineOfSight(slot, eye[, softness])` gates a slot the vision calls returned; it needs `FOG_LOS_BUILD`. `setVisionCeiling(slot, height[, fade])` / `getVisionCeiling(slot)` cap a slot upward (negative height = off; every slot starts off); `setRevealSurfaceTreatment(density[, capTone])`, `getRevealSurfaceTreatment()` → `on, density, tone`, and `clearRevealSurfaceTreatment()` own the canvas's partial-cut dissolve and cap tone, both in [0, 1]; all raise named errors, state unchanged.

- **`IRFile`:** `bindLuaFiles()`, opt-in. `readText` → string or nil, `writeText` → bool (makes parent dirs), `mtime` → number (0 if missing). Run-dir-relative only; absolute/`..` paths are rejected; `io`/`os` stay closed.
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
- **`IRRender.createViewport` family:** `LuaScript::bindLuaViewport()`, opt-in like `IRFog`;
  every setter takes the id `createViewport` returned ([design](../../docs/design/secondary-viewport.md)).

## Commands and input (`IRCommand.*`, `IRInput.*`)

`LuaScript::bindLuaCommands()`; design in [`docs/design/lua-input-commands.md`](../../docs/design/lua-input-commands.md).
It also binds the `IRInput.onEntity*` hover/click handlers ([input prefabs](../prefabs/irreden/input/CLAUDE.md)).
Compose modifiers with `bit.bor`. A `createCommand` body appears in the F1 overlay only with
`name`/`description`; `isButtonBound` is modifier-blind. A new prefab command needs an
`IR_BIND_CMD` line in `lua_command_bindings.hpp` and a case in `ir_command.cpp`'s `fireByName`/`bindPrefabCommand`.

## Prefab format (`Prefab.register`, `Prefab.spawn`)

A prefab has optional refs/rotation/canvas/components/setup and v2 `parts`
([schema](../../docs/design/lod-strategy.md), Phase 2); an `IRComponent.register` name in `components`
spawns only in a process that registered it. Contracts: [`prefab_api.hpp`](include/irreden/script/prefab_api.hpp).

## Script output

`LuaScript` binds `print` to `ScriptLog`: one timestamped line per call, arguments
tab-joined verbatim, ordered with engine/client logs, and flushed (load-bearing under
redirected stdout and signal death). `io.write` and a bare `sol::state` stay buffered.

## Script resolution

`scriptFile(path)` passes the path to sol2 unchanged; relative paths resolve from
cwd ([`BUILD.md`](../../docs/agents/BUILD.md) §"Running an executable").

## Gotchas

- `registerTypeFromTraits<T>()` without its `_lua.hpp` include is a link error.
- `IRScript::*FromLua` (`ir_script_utils.hpp`) default instead of raising, so
  check the type first; `sol::object::is<sol::table>()` is true for userdata.
- Batch-create factories must match C++ arity ([`cpp-ecs-smells.md`](../../.claude/rules/cpp-ecs-smells.md)).
- `LuaScript` lifetime is absolute: its destruction invalidates every Lua handle.
- Bind a lambda whose captures need destruction through `detail::statefulLuaFunction`
  (`ir_script_utils.hpp`): sol2 keys `__gc` by type name, and GCC lambda names collide.
