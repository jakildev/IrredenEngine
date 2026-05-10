# engine/script/ — LuaJIT 2.1 via sol2

Thin wrapper around sol2 that exposes ECS components, math, and input to
Lua. Creations register *which* components get bound via a per-creation
`lua_component_pack.hpp` file.

## Lua runtime: LuaJIT 2.1

The engine's Lua VM is **LuaJIT 2.1** (Lua 5.1 base + LuaJIT extensions).
The runtime is fetched at configure time and built from upstream source
via `engine/script/third_party/luajit/CMakeLists.txt`.

What's available beyond Lua 5.1:

- `bit` module — bitwise ops over 32-bit integers (LuaJIT-native, faster
  than Lua 5.4's `>>`/`<<`/`&`/`|` operators when JIT-compiled).
- `ffi` module — C FFI from Lua. Available to creations; engine bindings
  stay sol2-based.
- Trace JIT compiler — heats up stable inner loops into compiled native
  code. Per-archetype Lua system ticks are exactly the shape it
  compiles well; expect 2–10× C++ on warmed-up loops vs. ≥10 000× under
  the prior Lua 5.4 + sol2 path.

What's NOT available (Lua 5.2/5.3/5.4 features, do not use):

- `goto` / labels — LuaJIT runs in 5.1-compat mode by default.
- Integer subtype — all numbers are doubles; whole-number literals
  written `0`, `0.0`, or `1.0` round-trip identically through sol2 as
  int. For float fields whose default is whole-numbered (`x = 0.0`)
  use the explicit form `x = { type = "float", default = 0 }`. Auto-
  inference keys off whether the value has a fractional part.
- `<const>` / `<close>` attribute syntax — Lua 5.4 only.
- Generational GC tuning knobs — LuaJIT uses incremental.
- `bit32` (the 5.2 transitional module) — use `bit` instead, same
  function names with `bit.bxor` etc.
- `math.type` — there is no integer subtype to query.

Sol2 detects LuaJIT via `LUAJIT_VERSION` in `lua.h` and adapts
bindings automatically; no creation-side code changes were required
for the runtime swap. See [`docs/design/lua-driven-ecs.md`](../../docs/design/lua-driven-ecs.md)
"Retrospective" for the perf-measurement story.

### Build flag: `SOL_EXCEPTIONS_ALWAYS_UNSAFE`

`engine/script/CMakeLists.txt` defines `SOL_EXCEPTIONS_ALWAYS_UNSAFE=1`
on `IrredenEngineScripting`. Sol2's default for LuaJIT 2.1 lets C++
exceptions propagate through the LuaJIT VM via the platform unwinder —
in practice, the message is lost and `sol::error::what()` reports a
generic `"C++ exception"`. With `ALWAYS_UNSAFE` the trampoline catches
`std::exception` in-process and forwards `what()` via `lua_error`,
preserving the message in `protected_function` results. The
"unsafe" name is sol2 convention; it is the safer choice here.

LuaJIT itself is compiled with `XCFLAGS=-DLUAJIT_UNWIND_EXTERNAL` so
external unwinding is wired up regardless of the sol2 setting — a
future binding that prefers the propagation path can flip the
sol2 macro without rebuilding LuaJIT.

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
  so Lua scripts can pass them to `IRModifier.add` (or use the field
  name string `"Hp.current"` directly — both shapes are accepted; see
  the modifier surface below). String / function / table fields receive
  `kInvalidFieldId` (not modifier-targetable).
- **Storage:** a Lua-typed component is one `IComponentDataLuaTyped`
  impl with one native vector column per declared field
  (`std::vector<int32_t>`, `std::vector<float>`, etc., per field type).
  Reading or writing a field is a typed vector index — never a per-frame
  `sol::table` lookup.

### Two-tier accessor contract

**Table-style** (`addLuaComponent(overrides)`, `getLuaComponent`) — friendly
setup/debug surface. `getLuaComponent` allocates a Lua table per call and does
string-keyed field lookups. Fine for one-shot calls and inspection; not for
per-tick hot paths.

**Index-style** (`getLuaField`, `setLuaField`) — zero-string hot-path surface.
Resolve `field.index` once at script load and pass the integer per tick. No
string lookup, no table allocation:

```lua
local C_Hp = IRComponent.register("Hp", { current = 100 })
local currentIdx = C_Hp.fields.current.index  -- resolve once

-- inside per-tick system body:
local v = IREntity.getLuaField(entity, C_Hp, currentIdx)
IREntity.setLuaField(entity, C_Hp, currentIdx, v + 1)
```

Both accessors bounds-check `fieldIndex` and raise a Lua error on
out-of-range. Use index-style inside any per-tick Lua system body.

The full design is in [`docs/design/lua-driven-ecs.md`](../../docs/design/lua-driven-ecs.md).

## Build-time codegen of Lua-defined components (CODEGEN mode)

The same `IRComponent.register("Name", { ... })` schema can be **statically
emitted** as a C++ struct + Lua usertype binding at build time, instead of
allocated at runtime via `IComponentDataLuaTyped`. Components-only path is
T-106; system bodies (T-107) and per-system mode override (T-108) follow.

**Build pipeline:**

```cmake
# In a creation's or test's CMakeLists.txt:
irreden_lua_codegen(<target>
    SOURCES path/to/schema.lua
    OUTPUT_HPP ${CMAKE_CURRENT_BINARY_DIR}/codegen/my_components.hpp
)
```

The helper invokes `cmake/lua_codegen/ir_lua_codegen` (a small C++ tool
linking sol2 + the engine's vendored Lua) which executes the input schema
under stubbed `IRComponent` / `IRSystem` / `IRTime` / etc. globals, captures
every `IRComponent.register(...)` call, and emits a header with one
`namespace IRComponents { struct C_Name { ... }; }` per registration plus
the matching `kHasLuaBinding` + `bindLuaType` specializations and a
`IRScript::CodegenRegistry::registerCodegenComponents(LuaScript&)` helper
that pre-registers each component with the EntityManager and binds the
usertype.

**CODEGEN supports:** `int32` / `float` / `bool` / `string` field types and
both the short form (`current = 100`) and the explicit-type form
(`current = { type = "float", default = 100 }`).

**EVAL-only (codegen-time error if used in CODEGEN):** `table` and
`function` field types, nested-table short form, duplicate component names,
unknown `type` tag. Errors point at file/component/field with a
Lua-stack traceback.

**Field order is alphabetical** in the generated struct + constructor —
Lua hash-table iteration is implementation-defined and not stable, so
the codegen sorts fields by name to keep the header reproducible. Use
positional construction `C_Hp{50, 75}` or named field assignment after a
default-construct.

The hand-test + regression coverage lives at
`test/script/lua_component_codegen_test.cpp` + the sibling fixture
`lua_component_codegen_fixtures.lua`. The architect plan
(`docs/design/lua-driven-ecs.md` / planning artifacts under `~/.claude/`)
covers the broader CODEGEN/EVAL split design.

### CODEGEN system bodies (T-107)

The same `irreden_lua_codegen()` invocation also picks up
`IRSystem.registerSystem({...})` calls and emits typed C++ tick bodies.
The codegen tool slices the `tick = function(arch) ... end` source via
Lua's `lua_getinfo` debug API, parses the body against a fixed DSL
subset, and emits one
`IRScript::CodegenRegistry::createSystem_<NAME>()` per system that wraps
a typed `IRSystem::createSystem<...>` call. The generated header also
exposes a `CodegenSystemIds registerCodegenSystems()` aggregate registry
so creations / tests can register every codegen'd system in one call.

**DSL subset accepted in CODEGEN system bodies:**

- One canonical loop only: `for i = 0, arch.length - 1 do ... end`. No
  generic-for `pairs/ipairs`, no negative steps, no nested archetype
  loops, no `while`/`repeat`/`break`/`goto`.
- Column ops on **Lua-defined** components only (declared via
  `IRComponent.register` in the same codegen run): `arch.Comp:at(i)`,
  `arch.Comp:setAt(i, value)`, `arch.Comp:getField(i, "fieldName")`,
  `arch.Comp:setField(i, "fieldName", value)`. Field names must be
  string literals — dynamic field names belong in EVAL.
- `Comp.new(args...)` constructor calls. Argument order matches the
  alphabetically-sorted field order in the codegen'd struct.
- Math + comparisons + branches: `+ - * / %`, `< > <= >= == ~=`, `and
  or not`, `if`/`elseif`/`else`/`end`. `^` (exponent) is rejected —
  use a math.* intrinsic.
- `local <name> = <expr>` declarations with simple-RHS init. Multi-target
  `local a, b = ...` is rejected.
- Whitelisted intrinsics — call as `math.sin(x)` / `IRMath.lerp(a,b,t)`
  / etc. The current set lives in
  `cmake/lua_codegen/system_dsl.cpp`'s `kIntrinsicRegistry`. Adding an
  entry is one line: Lua name, C++ expression head, arity. `math.*`
  routes to `IRMath::*` (the engine's math wrapper layer — the
  CODEGEN-emitted code never calls `std::sin` etc. directly).

**Forbidden in CODEGEN bodies (build-time error pointing at file:line):**

- C++-bound types in column ops — `arch.TestPos:at(i)` errors if
  `TestPos` is a hand-written C++ struct rather than a Lua-defined
  one. Mark the system `mode = "eval"` (T-108) or move the type's
  declaration to Lua.
- Closures capturing external upvalues, metatables, dynamic dispatch
  (`arch.foo[name](...)`), `require`, table constructors beyond
  `Comp.new`, varargs, `nil`, `_` length operator, side-effecting
  bare expression statements.

**`excludes = { 'Tag' }`** at registration time materialises as
`IRSystem::Exclude<C_Tag>` in the generated `IRSystem::createSystem<...>`
template parameter list. Same archetype-filter behavior as a hand-written
prefab system, no per-entity branching.

**Test-harness toggle:** EVAL coverage continues at
`test/script/lua_system_register_test.cpp`. CODEGEN coverage is at
`test/script/lua_system_codegen_test.cpp` + the sibling
`lua_system_codegen_fixtures.lua`. Cases that depend on EVAL-unique
features (mixed C++/Lua component archetypes via runtime-typed
dispatch, `replaceSystemBody` hot-reload, `entityAt(i)` reads,
dynamic field-name strings) stay in the EVAL test file by design.

**v1 scope notes:**

- Codegen system create-functions return a fresh `SystemId` each time
  they're called. Hot-reload via `IRSystem.replaceSystemBody` is
  EVAL-only (T-108); CODEGEN systems are static at build time.
- The `SystemName`-enum partial generation called out in #587's
  acceptance is deferred — codegen systems register by string name
  through `IRSystem::createSystem<...>` directly, which produces a
  valid `SystemId` in the same id space and works with `SystemManager`'s
  pipeline machinery without going through `System<NAME>::create()`.
  Add the enum partial in a follow-up if a creation needs the named
  prefab-system spelling for a codegen'd system.

### Per-system mode override + CODEGEN/EVAL coexistence (T-108)

Every `IRSystem.registerSystem({...})` call accepts an optional
`mode` field that overrides the creation default for that one system:

```lua
-- CODEGEN system: typed C++ tick body emitted at build time
IRSystem.registerSystem({
    name = "Move",
    components = { "Pos", "Vel" },
    tick = function(arch) ... end,        -- mode field absent → creation default
})

-- EVAL system in the same file: runtime-registered via createSystemDynamic,
-- hot-reloadable via replaceSystemBody. Skipped by the codegen tool's C++
-- emission pass; the runtime LuaScript registers it the moment scriptFile()
-- evaluates the call.
IRSystem.registerSystem({
    name = "Wobble",
    mode = "eval",
    components = { "Pos" },
    tick = function(arch) ... end,
})
```

**Mode resolution.** Two layers, evaluated independently at build time
and runtime:

- **Build time** — the codegen tool's `--default-mode=codegen|eval` CLI
  flag (driven by the `irreden_lua_codegen()` helper's `DEFAULT_MODE`
  param, which falls back to the `IR_LUA_ECS_DEFAULT_MODE` CMake cache
  var, which defaults to `CODEGEN`). The codegen tool emits typed
  `IRSystem::createSystem<...>` for CODEGEN systems and skips C++
  emission for EVAL systems.
- **Runtime** — `LuaScript::setEcsDefaultMode(EcsMode)`. The runtime
  `IRSystem.registerSystem` shim consults this default for unmarked
  calls. Default is `EVAL` so creations that don't use codegen at all
  keep working without ceremony. Codegen-using creations call
  `lua.setEcsDefaultMode(IRScript::CodegenRegistry::kDefaultEcsMode)`
  after `registerCodegenComponents()` so runtime dispatch matches the
  build-time default.

Unknown values in either layer (`mode = "potato"`, `--default-mode=lua`)
are explicit errors — silent fallback would hide the typo until
months later when the missing system surfaces.

**CMake helper.** Per-creation override via `DEFAULT_MODE` arg:

```cmake
irreden_lua_codegen(MyCreation
    SOURCES schema.lua
    OUTPUT_HPP ${CMAKE_CURRENT_BINARY_DIR}/codegen/my_codegen.hpp
    DEFAULT_MODE EVAL          # optional; defaults to IR_LUA_ECS_DEFAULT_MODE or CODEGEN
)
```

A dev-iteration build flavor can flip every creation's default at the
command line without editing source: `cmake -DIR_LUA_ECS_DEFAULT_MODE=EVAL`.

**Coexistence wiring.** A creation that mixes CODEGEN and EVAL systems
in one .lua file calls the helpers in this order:

```cpp
m_lua.bindLuaDrivenEcs();
IRScript::CodegenRegistry::registerCodegenComponents(m_lua);
m_lua.setEcsDefaultMode(IRScript::CodegenRegistry::kDefaultEcsMode);
auto codegenIds = IRScript::CodegenRegistry::registerCodegenSystems();
m_lua.scriptFile("main.lua");   // EVAL systems register here
// codegenIds.<Name> is the SystemId for each CODEGEN system; EVAL
// SystemIds come back from `IRSystem.registerSystem` in Lua.
```

The runtime pass over `main.lua` re-runs every `IRSystem.registerSystem`
call from the file, but unmarked-or-explicit-codegen calls are skipped
by the runtime shim so CODEGEN systems aren't double-registered. EVAL
calls register normally.

**`IRComponent.register` is idempotent in coexistence mode.** When
`registerCodegenComponents` has already pre-registered a component as
a C++ struct, the runtime `IRComponent.register("MyComp", {...})` call
returns the existing handle instead of erroring on duplicate. Without
the carve-out, the runtime load would create a parallel
`IComponentDataLuaTyped` under the same Lua name, splitting archetype
storage. Lua-side code that depends on `handle.fields.<name>.bindingId`
(modifier framework) does NOT work for codegen'd-as-C++ components —
the C++-side registration doesn't populate the modifier registry the
way the runtime path does.

**Hot-reload contract.** `IRSystem.replaceSystemBody(systemId, newTick)`
works on EVAL systems; calling it on a CODEGEN system raises a Lua
error pointing at the `mode = "eval"` escape hatch. Mark the system
EVAL or rebuild the binary.

**Test-harness coverage.** End-to-end coexistence regressions live in
`test/script/lua_system_coexistence_{test.cpp,fixtures.lua}` —
verifies both modes register, both tick, EVAL is hot-reloadable,
CODEGEN is not, and `IRComponent.register` is idempotent in the
coexistence path.

## Lua-defined systems (`IRSystem.registerSystem`)

`bindLuaDrivenEcs()` also exposes `IRSystem.registerSystem`. A Lua
system is a runtime-archetype-typed dispatch: include / exclude
component sets and a tick body, both resolved against the same
`ComponentId` space as C++ components.

```lua
local sysId = IRSystem.registerSystem({
    name = "MoveByVelocity",
    components = { IRComponent.C_Position3D, IRComponent.C_Velocity3D, C_Marker },
    excludes = { IRComponent.C_NoMove },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local pos = arch.C_Position3D:at(i)
            local vel = arch.C_Velocity3D:at(i)
            arch.C_Position3D:setAt(i, C_Position3D.new(pos.x + vel.x, pos.y, pos.z))
        end
    end,
})
```

- **Component handle rule.** Always reference engine C++ components in
  `components` / `excludes` lists using the `IRComponent.C_Name` handle,
  never as a bare string like `"C_Position3D"`. After `bindLuaDrivenEcs()`
  + `registerType<T>("C_Name", ...)`, `IRComponent.C_Name` is a handle
  table `{ typeName, componentId }` identical in shape to what
  `IRComponent.register` returns for Lua-defined components. Both forms
  resolve via `resolveComponentEntry` in `lua_script.cpp`. Using handles
  instead of strings means a typo or missing `registerType` call surfaces
  immediately as a nil-access error at startup, not a silent no-match at
  runtime.
- **Component name resolution (resolver details).** The resolver accepts:
  1. `IRComponent.C_Name` (table with `componentId`) — preferred for
     C++ components registered by the creation's `lua_component_pack`.
  2. The handle returned by `IRComponent.register("Hp", {...})` — for
     Lua-defined components; assign the returned handle to a local and
     pass it directly (e.g. `local C_Hp = IRComponent.register(...)`
     then `components = { C_Hp }`).
  3. Bare strings — still accepted by the resolver but discouraged for
     C++ components (use handle form instead). Strings remain the only
     option for Lua-defined components whose handle is not in scope.
  Any entry that doesn't resolve raises a Lua error pointing at the
  missing `registerType` or `IRComponent.register` call.
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
- **Return value.** A `SystemId` (`lua_Integer`). Pass it directly to
  `IRSystem.registerPipeline` alongside prefab-system ids returned by
  `IRSystem.systemId(SystemName.X)` to mix Lua-defined and C++ systems
  in one pipeline.
- **No begin/end ticks yet.** Lua-side `beginTick` / `endTick`
  hooks are not exposed; add them when a use case needs
  frame-scoped setup or teardown.

## Hot-reload of Lua system bodies (`IRSystem.replaceSystemBody`)

`IRSystem.replaceSystemBody(systemId, newTick)` reseats the tick
body of an already-registered Lua system in place. The system's
`SystemId`, archetype filter, exclude archetype, `SystemParams`, and
pipeline registrations are unchanged — only the function invoked per
matched archetype changes. Subsequent ticks on `systemId` use
`newTick` with no special handling for in-flight entities.

```lua
local sysId = IRSystem.registerSystem({
    name = "Regen",
    components = { "C_Hp" },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local hp = arch.C_Hp:getField(i, "current")
            arch.C_Hp:setField(i, "current", hp + 1)  -- regen rate = 1
        end
    end,
})

-- Edit main.lua, change the regen rate to 5, then call:
IRSystem.replaceSystemBody(sysId, function(arch)
    for i = 0, arch.length - 1 do
        local hp = arch.C_Hp:getField(i, "current")
        arch.C_Hp:setField(i, "current", hp + 5)  -- new regen rate
    end
end)
```

- **Eligibility.** Only systems registered via `IRSystem.registerSystem`
  carry the captured tick reference; calling
  `replaceSystemBody` on a prefab system id (from
  `IRSystem.systemId(SystemName.X)`) or any other `SystemId` raises
  a Lua error pointing at `IRSystem.registerSystem`.
- **Component-schema hot-reload is out of scope** for this surface.
  Only the per-archetype body changes; the include / exclude
  archetype is fixed at `registerSystem` time. Re-running
  `IRSystem.registerSystem` with the same name creates a *new*
  `SystemId` — there is no in-place archetype migration.
- **C++ parallel.** `IRSystem::replaceSystemBody(systemId, body)` is
  the C++ entry point (see `engine/system/include/irreden/ir_system.hpp`).
  Both APIs share `SystemManager::replaceSystemBody`, which mutates
  `m_ticks[id].functionTick_` directly. The Lua surface goes one
  level higher: it reseats the captured `sol::protected_function`
  inside the existing body lambda (via a shared_ptr), keeping the
  archview / column-view setup intact.

## Pipeline composition (`IRSystem.registerPipeline`, `IRSystem.SystemName`)

`bindLuaDrivenEcs()` also exposes the pipeline-composition surface so a
creation's entire `initSystems` can live in Lua. The C++ side registers
which prefab systems Lua may spell:

```cpp
// In the creation's lua-binding callback:
script.bindLuaDrivenEcs();
script.registerPrefabSystems<
    IRSystem::LIFETIME,
    IRSystem::GLOBAL_POSITION_3D,
    IRSystem::FRAMEBUFFER_TO_SCREEN
>();
// Also valid: cache an externally-created prefab id under its enum name.
auto resolver = IRPrefab::Modifier::registerResolverPipeline();
script.registerPrefabSystemId(IRSystem::MODIFIER_DECAY, resolver.modifierDecay_);
// ... one call per resolver SystemId.
```

Then in Lua:

```lua
local SystemName = IRSystem.SystemName

local luaSysId = IRSystem.registerSystem({
    name = "MyLuaSys",
    components = { IRComponent.C_Position3D },
    tick = function(arch) ... end,
})

IRSystem.registerPipeline(IRTime.UPDATE, {
    IRSystem.systemId(SystemName.GLOBAL_POSITION_3D),
    IRSystem.systemId(SystemName.LIFETIME),
    luaSysId,
    IRSystem.systemId(SystemName.MODIFIER_DECAY),
    IRSystem.systemId(SystemName.MODIFIER_RESOLVE_GLOBAL),
    IRSystem.systemId(SystemName.MODIFIER_RESOLVE_EXEMPT),
})
```

- **`IRTime.{UPDATE, RENDER, INPUT, START, END}`** — pipeline event tags.
  Always spell these via `IRTime.X` in Lua (never a bare integer literal).
  On the C++ binding side, new event entries must be added via the
  `IR_BIND_TIME(name)` macro in `bindIRTimeEvents` (not a hand-written
  string literal) so the Lua table key stays derived from the enum name.
- **`IRSystem.SystemName.X`** — every prefab-system enum value, exposed as
  an integer table. The list lives in
  `engine/script/include/irreden/script/lua_pipeline_bindings.hpp`; new
  prefab systems must be appended there alongside the
  `engine/system/include/irreden/system/ir_system_types.hpp` entry
  using the `IR_BIND_SYS(name)` macro (not a hand-written string literal),
  and deleted values must be removed from both.
- **`IRSystem.systemId(name)`** — returns the cached `SystemId` for a
  prefab system the C++ side registered via
  `LuaScript::registerPrefabSystem<N>()` or
  `registerPrefabSystemId(name, id)`. Raises a Lua error pointing at the
  missing C++ registration if the name was never wired up.
- **`IRSystem.registerPipeline(event, ids)`** — accepts any mix of prefab
  ids (from `systemId`) and Lua-defined ids (from `registerSystem`).
- **Game-side enums** (e.g. `IRGameSystem.GameSystemName`) extend the
  same pattern — bind the game's own enum table at game-side init via
  `LuaScript::registerEnum<...>()` plus `registerPrefabSystemId` for each
  exposed game system. Engine code stays oblivious; Lua spells one
  pipeline that carries both.

The canonical example is `creations/demos/lua_pipeline_demo/`: zero C++
`initSystems`, every pipeline composed from `main.lua`.

## Modifier framework (`IRModifier.*`)

`bindLuaDrivenEcs()` exposes the engine modifier framework as
`IRModifier`. The full surface:

```lua
IRModifier.Transform.{ADD, MULTIPLY, SET, CLAMP_MIN, CLAMP_MAX, OVERRIDE}

IRModifier.registerField("Movement.speed")           -- → FieldBindingId
IRModifier.fieldId("Movement.speed")                 -- → FieldBindingId
IRModifier.fieldName(id)                             -- → string | nil

IRModifier.add(entity, fieldNameOrId, {
    transform = IRModifier.Transform.ADD,
    value = 0.5,
    source = sourceEntity,           -- optional
    ticks = 60,                      -- optional, -1 = no decay
})
IRModifier.addGlobal(fieldNameOrId, opts)
IRModifier.addLambda(entity, fieldNameOrId, fn, opts)
IRModifier.removeBySource(sourceEntity)

IRModifier.applyToField(entity, fieldNameOrId, base) -- → resolved float
IRModifier.resolved(entity, fieldNameOrId, fallback) -- read C_ResolvedFields
```

- **`fieldNameOrId`** — accepts a string (resolved against the registry
  every call — fine for cold paths and config; cache the integer for
  hot loops) or a `FieldBindingId`. Lua-defined components auto-expose
  their scalar field ids at `Comp.fields.<name>.bindingId`; pass that
  directly to skip the name lookup.
- **`entity` / `source`** — raw `EntityId` integers, the same shape as
  `arch.entityAt(i)` in a Lua system tick or `LuaEntity.entity`.
- **Resolver pipeline integration.** `IRModifier.add` only writes to
  `C_Modifiers`; the resolver systems (`MODIFIER_DECAY`,
  `MODIFIER_RESOLVE_GLOBAL/EXEMPT`, etc.) are what compose
  `C_ResolvedFields` once per UPDATE. To see resolved values from Lua,
  splice those system ids into the UPDATE pipeline (after a one-shot
  `IRPrefab::Modifier::registerResolverPipeline()` call from C++ —
  the function creates the singleton globals entity and returns the
  six resolver SystemIds; cache them via `registerPrefabSystemId`).
- **`IRModifier.applyToField`** is a direct query; it shares one
  evaluator with the resolver pipeline, so the two paths agree on the
  same input regardless of whether the resolver tick has run yet.

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
- **`registerType` name must match the C++ class name exactly.** The string
  passed to `registerType<C_Foo, ...>("C_Foo", ...)` becomes the Lua-visible
  name AND the `IRComponent.C_Foo` handle key. Using a non-canonical name
  (e.g. `"Foo"` for `C_Foo`) breaks the `IRComponent.C_Foo` spelling and
  causes confusing nil-access errors. Convention: always pass the literal
  class name as the binding string.
- **`IRComponent.C_Name` requires `bindLuaDrivenEcs()` first.** The handle
  is populated in `recordComponentLuaName`, which only writes to
  `IRComponent[name]` if that table already exists. Call
  `script.bindLuaDrivenEcs()` before any `script.registerType<T>()` call
  that expects Lua code to reference the component by handle.
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
