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
    script.registerTypesFromTraits<C_LocalTransform, C_ZoomLevel /* ... */>();
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

-- Singleton-component access (one entity per component type, lazily
-- created and cached by ComponentId; see engine/entity/CLAUDE.md):
local rules = IREntity.singleton(C_Hp)                    -- LuaEntity
IREntity.setLuaField(rules, C_Hp, C_Hp.fields.current.index, 100)
```

**Deferred entity create/destroy (mid-tick, #2286).** A structural change
issued from inside a system tick must be deferred, exactly like a C++ system
(archetype iteration is live). `IREntity.deferredCreate` / `deferredDestroy`
queue the op through the engine's deferred machinery — the create's insert
drains at the next `flushStructuralChanges` (group boundary), the destroy at
`destroyMarkedEntities` (pipeline end):

```lua
-- Optional component list: { componentDef, overridesTableOrNil } entries.
-- componentDef is any IRComponent.register / codegen'd handle (carries
-- componentId); overrides apply exactly like addLuaComponent.
local eid = IREntity.deferredCreate({ { C_Hp, { current = 30 } } })  -- → EntityId
IREntity.deferredDestroy(eid)   -- eid is a raw EntityId (e.g. arch.entityAt(i))
```

The returned EntityId is reserved immediately (usable to destroy or stash the
entity), but the entity does not materialize until the flush. Attaches any
component addressable from Lua by `componentId`; a native C++-only component
(no Lua-typed default-row support) still uses the templated C++
`createEntity`. See `docs/design/lua-driven-ecs.md` §G4.

- **Type inference:** integer literal → `int32`; float literal → `float`;
  string → `std::string`; bool → `bool`; function → `sol::function`. The
  short form fails registration with a Lua error if a default value isn't
  natively classifiable; nested tables in the short form are intentionally
  rejected. Use the explicit `{ type = "...", default = ... }` form to
  disambiguate (`current = { type = "float", default = 100 }`), to opt
  in to opaque table storage (`payload = { type = "table", default = {} }`),
  or to declare a packed `vec3` / `ivec3` / `vec4` field (`pos = { type =
  "vec3", default = { x = 0, y = 0, z = 0 } }` — see "Packed vec3 / ivec3 /
  vec4 fields" below; `quat` / `quaternion` are register-time aliases for a
  `vec4` column, EVAL-only for now).
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
  (`std::vector<int32_t>`, `std::vector<float>`, `std::vector<IRMath::vec3>`,
  etc., per field type). Reading or writing a field is a typed vector index —
  never a per-frame `sol::table` lookup.

### Packed vec3 / ivec3 / vec4 fields (#1368, design G1a; #2163 vec4/quat)

A field can declare a packed `vec3` / `ivec3` / `vec4` kind instead of
hand-flattening to `_x/_y/_z` scalars. Declared via the explicit-tag form with
an `{ x, y, z }` (or positional `{ 1, 2, 3 }`) default:

```lua
local C_Body = IRComponent.register("Body", {
    pos  = { type = "vec3",  default = { 0, 0, 0 } },
    cell = { type = "ivec3", default = { x = 0, y = 0, z = 0 } },
})
```

- **EVAL** stores a real `std::vector<IRMath::vec3>` / `ivec3` column —
  byte-identical to a hand-C++ `vec3` field. The table-style accessors surface
  a value as an `{ x, y, z }` Lua table (`getLuaComponent`, `getLuaField`); the
  write accessors (`setLuaField`, `addLuaComponent` overrides) accept an
  `{ x, y, z }` table **or** an `IRMath::vec3` userdata. The friendly read
  allocates a 3-key table per call — fine for setup/inspection, not a per-tick
  hot path; a zero-alloc hot path uses three scalar fields instead.
- **`vec4` / `quat` / `quaternion` (EVAL, #2163).** All three tags declare one
  `std::vector<IRMath::vec4>` column — a quaternion is a `vec4`
  engine-wide (`IRMath::vec4 = glm::vec4(qx,qy,qz,qw)`, `.w` the scalar), so
  `quat` / `quaternion` are register-time aliases of `vec4`, not a second
  storage type. Reads surface as an `{ x, y, z, w }` table; writes accept an
  `{ x, y, z, w }` / `{ 1, 2, 3, 4 }` table **or** an `IRMath::vec4` userdata.
  An omitted or partial default resolves to the identity quat `(0, 0, 0, 1)`
  via `quatFromLua` (not the zero-vec that `vec3` defaults to) — the correct
  default for the primary rotation consumer; a plain-`vec4` author passes an
  explicit 4-element default. **EVAL-only:** the CODEGEN tool does not yet emit
  a `vec4` struct member, so a `vec4` / `quat` field in a CODEGEN `.lua` still
  errors as an unknown tag (same status as `vec2`); keep such a component in
  `mode = "eval"`.
- **CODEGEN** emits a real `IRMath::vec3` / `ivec3` struct member. Inside a
  CODEGEN tick body, read a packed field's components with `.x` / `.y` / `.z`
  and build a fresh value with the `vec3.new(x, y, z)` / `ivec3.new(x, y, z)`
  built-in constructors (the only DSL way to write a packed field — there is no
  whole-vector arithmetic; operate on components):

  ```lua
  local p = arch.Body:getField(i, "pos")
  arch.Body:setField(i, "pos", vec3.new(p.x + vx, p.y + vy, p.z + vz))
  ```

- **Not modifier-targetable.** Packed fields receive `kInvalidFieldId` — only
  scalar `int32` / `float` / `bool` fields auto-register a modifier binding.
  For modifier-driven vec3 offsets use the modifier framework's typed vec3
  fields (`IRModifier.registerFieldVec3`), not a component field binding.
- **Out of scope:** `vec2` (same mechanism, deferred until needed), CODEGEN
  emission for `vec4` / `quat` (EVAL-only above), and variable-length array
  fields (G1b: a bounded `"float[K]"` inline-array, or engine-owned data for
  genuinely ragged lists — see `docs/design/lua-driven-ecs.md`).

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

## Lua-defined enums (`IREnum.register`)

Closed enums can be defined in Lua, the Lua-native counterpart to the C++
`registerEnum` stopgap (`*_lua.hpp` + `kHasLuaBinding`). Bound by the same
`bindLuaDrivenEcs()` call, so no extra init:

```lua
local DeviceType = IREnum.register("DeviceType", { "EFFECT", "SYNTH", "CONTROLLER" })
DeviceType.EFFECT       -- 0
DeviceType.SYNTH        -- 1
IREnum.DeviceType.SYNTH -- same table, reachable by name from any file
```

- **Members map to 0-based ordinals** in declaration order — same numbering
  as a C++ `enum class` and the 0-based field `index` of
  `IRComponent.register`. The returned handle *is* the enum table, and the
  same table is also stored at `IREnum.<Name>` for cross-file reference.
- **Validated at registration, not silently.** A non-string member, an
  empty-string member, an empty member list, a duplicate member, a
  duplicate enum name, or the reserved name `"register"` raises a Lua error
  at the `IREnum.register` call. A *typo on access* (`DeviceType.EFEKT`) is
  plain Lua `nil` — push enum members up to load time by spelling them
  through the table, never as bare strings (see
  [`.claude/rules/cpp-lua-enums.md`](../../.claude/rules/cpp-lua-enums.md)).
- **Usable wherever a C++ `registerEnum` enum is** — the value is an
  integer, so `kind = DeviceType.SYNTH` is a valid `int32` component-field
  default, a function argument, etc.
- **No native storage.** Unlike `IRComponent.register`, an enum is a pure
  name→int table with nothing to lower to C++, so **CODEGEN and EVAL behave
  identically**: both build the table at runtime, and the build-time codegen
  tool carries a matching `IREnum.register` shim (sharing
  `IRScript::detail::buildLuaEnumTable`, `lua_enum_def.hpp`) so codegen-mode
  `.lua` files that reference enum members — e.g. as a component default —
  resolve to the *same* ordinals at build time and validate the same way.
- **Limitation:** an enum member used *inside a CODEGEN system tick body*
  (lowered by `system_dsl`) is not yet folded to a literal by the DSL —
  enums are for setup/identity/config and EVAL-mode logic in v1. Use a
  literal in CODEGEN tick bodies, or keep the enum-consuming system in EVAL.

## Build-time codegen of Lua-defined components (CODEGEN mode)

The same `IRComponent.register("Name", { ... })` schema can be **statically
emitted** as a C++ struct + Lua usertype binding at build time, instead of
allocated at runtime via `IComponentDataLuaTyped`. Three layers ship today:
components, system bodies, and per-system mode override (CODEGEN/EVAL
coexistence) — all driven by the same `irreden_lua_codegen()` helper.

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

**CODEGEN supports:** `int32` / `float` / `bool` / `string` / `vec3` / `ivec3`
field types and both the short form (`current = 100`) and the explicit-type
form (`current = { type = "float", default = 100 }`). Packed `vec3` / `ivec3`
fields require the explicit-tag form with an `{ x, y, z }` default (the codegen
tool has no `vec3` usertype to infer from a short-form value) and lower to real
`IRMath::vec3` / `ivec3` struct members — see "Packed vec3 / ivec3 / vec4 fields"
above for the tick-body `.x/.y/.z` + `vec3.new` DSL surface.

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

### CODEGEN system bodies

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
  CODEGEN-emitted code never calls `std::sin` etc. directly). These are
  value-returning — use them inside an expression (a `local`, a column
  setter, a branch cond); a bare call statement is rejected.
- Whitelisted **side-effecting engine bindings** (#1616) — a curated set
  of void pass-through engine setters callable as a **bare statement**
  (not inside an expression), e.g. `IRRender.setSunIntensity(x)` lowers
  to `IRRender::setSunIntensity(...)`. The current set is the
  `IRRender.*` render-glue setters (`setSunIntensity`, `setSunAmbient`,
  `setExposure`, `setSkyIntensity`, `setCameraZoom`), marked
  `isStatement_ = true` in `kIntrinsicRegistry`. This lets a system that
  *computes* a render parameter under CODEGEN also *apply* it inline,
  instead of stranding the application layer in EVAL or a C++ bridge.
  Each statement-binding entry carries the engine header that declares
  its C++ function (`requiredInclude_`); the codegen tool emits that
  `#include` in the generated header only when a body actually uses the
  binding. Adding a setter is one registry line + (if a new namespace)
  its header. Scalar args only — each arg lowers as a numeric expression.

**Forbidden in CODEGEN bodies (build-time error pointing at file:line):**

- C++-bound types in column ops — `arch.TestPos:at(i)` errors if
  `TestPos` is a hand-written C++ struct rather than a Lua-defined
  one. Mark the system `mode = "eval"` or move the type's declaration
  to Lua.
- Closures capturing external upvalues, metatables, dynamic dispatch
  (`arch.foo[name](...)`), `require`, table constructors beyond
  `Comp.new`, varargs, `nil`, `_` length operator. A bare call
  statement is rejected **unless** it targets a whitelisted
  side-effecting binding (above); a bare value-returning intrinsic
  (`math.sin(x)` as a statement) is still an error — it's a dropped
  assignment.
- **String formatting is not yet supported** — `string.format` / `..`
  concatenation in a CODEGEN tick body is rejected. The string-building
  half of #1616 is deferred; keep label/HUD systems that format strings
  in EVAL (`mode = "eval"`) for now. (Sibling gap: the IREnum-in-tick
  literal-folding limitation above.)

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
  EVAL-only; CODEGEN systems are static at build time.
- The `SystemName`-enum partial generation called out in #587's
  acceptance is deferred — codegen systems register by string name
  through `IRSystem::createSystem<...>` directly, which produces a
  valid `SystemId` in the same id space and works with `SystemManager`'s
  pipeline machinery without going through `System<NAME>::create()`.
  Add the enum partial in a follow-up if a creation needs the named
  prefab-system spelling for a codegen'd system.

### Per-system mode override + CODEGEN/EVAL coexistence

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

**Future-hook note.** The codegen tool emits a
`IRScript::CodegenRegistry::kEvalSystemNames[]` array carrying the
names of every EVAL system declared in the codegen run. The intent
is a runtime verification loop at script-eval boundary (each name
must register, otherwise raise — catches typos / mode mismatches at
startup rather than months later). The array is emitted today as a
prepared hook; no consumer reads it yet. File a follow-up task if
the verification loop proves load-bearing for a creation.

## Lua-defined systems (`IRSystem.registerSystem`)

`bindLuaDrivenEcs()` also exposes `IRSystem.registerSystem`. A Lua
system is a runtime-archetype-typed dispatch: include / exclude
component sets and a tick body, both resolved against the same
`ComponentId` space as C++ components.

```lua
local sysId = IRSystem.registerSystem({
    name = "MoveByVelocity",
    components = { IRComponent.C_LocalTransform, IRComponent.C_Velocity3D, C_Marker },
    excludes = { IRComponent.C_NoMove },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local xf  = arch.C_LocalTransform:at(i)
            local vel = arch.C_Velocity3D:at(i)
            arch.C_LocalTransform:setAt(i, C_LocalTransform.new(vec3(xf.translation_x + vel.x, xf.translation_y, xf.translation_z)))
        end
    end,
})
```

- **Component handle rule.** Always reference engine C++ components in
  `components` / `excludes` lists using the `IRComponent.C_Name` handle,
  never as a bare string like `"C_LocalTransform"`. After `bindLuaDrivenEcs()`
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
- **C++-component per-field writes from Lua.** When a `*_lua.hpp`
  binding wants `:at(i).field = v` to write a single field through the
  column ref (not a whole-row `setAt`), how the field binds depends on
  its type:
  - **Scalar** (`float`/`int`/`bool`) — bind as a read-write **member
    pointer** (`"input", &C_RotationTarget::input_`); sol2 makes it a
    directly-mutable property and the write lands in the referenced row.
  - **Math-typed** (`IRMath::vec3`/`vec4`/`Color`) — bind as
    `sol::property(getter, setter)` where the setter routes a Lua
    `{ x, y, z, w }` table (or an IRMath userdata) through the matching
    `IRScript::*FromLua` converter (`ir_script_utils.hpp`) and the getter
    returns a fresh `{ x, y, z[, w] }` table. A bare
    `&C_Foo::vecField_` member pointer compiles but is **unusable** from
    Lua: `IRMath::vec3`/`vec4` are not registered Lua usertypes, so Lua
    can neither construct a value to assign nor read the returned
    userdata. The table convention mirrors the Lua-defined packed-vec
    read/write shape (see "Packed vec3 / ivec3 / vec4 fields" above), so
    C++-component and Lua-defined math fields round-trip through Lua
    identically. `C_LocalTransform` (`rotation`/`translation`/`scale`)
    is the reference binding. The getter allocates a small table per read
    — keep any zero-alloc scalar convenience getters (e.g.
    `translation_x/y/z`) as the hot read path.
  - **Whole-object `setAt(i, T.new(...))`** remains the fallback for a
    binding that hasn't exposed per-field properties.
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
- **Optional `concurrency` field** (T-223). Accepts the integer-typed
  `IRSystem.Concurrency.{SERIAL, PARALLEL_FOR, MAIN_THREAD}` enum
  value; default `SERIAL` matches the legacy behavior. The codegen
  path threads the value through to the emitted
  `IRSystem::createSystem<...>(...)` call's trailing concurrency
  arg — engine-side `detail::validateConcurrencyForAccess` runs at
  template instantiation time and FATALs on the same invalid
  combinations a hand-written C++ system would (`PARALLEL_FOR` +
  batch-form tick, `PARALLEL_FOR` + entity-id form without
  `ParallelSafe`, `PARALLEL_FOR` + `MainThread` tag). The EVAL path
  is more conservative: an EVAL system body is a
  `sol::protected_function` call into LuaJIT, and **both sol2 and
  LuaJIT's GC are single-threaded** — running an EVAL body on a
  worker thread is unsound. The runtime shim therefore forces
  `PARALLEL_FOR` → `MAIN_THREAD` (with a one-shot per-system warning
  naming the spec) so the misuse surfaces in the log. Per the
  [`cpp-lua-enums`](../../.claude/rules/cpp-lua-enums.md) rule the
  field rejects string values (`concurrency = "parallel_for"`) with
  a diagnostic pointing at the `IRSystem.Concurrency.*` spelling;
  out-of-range integers raise an explicit error.

  The codegen tick body emits the per-component form: the canonical
  `for i = 0, arch.length - 1 do ... end` outer loop is dropped at
  emission time and the inner `arch.Comp:at(i)` / `setAt(i, ...)` /
  `getField(i, "f")` / `setField(i, "f", ...)` column ops lower to
  per-row `_ir_row_<CompName>` references, producing a
  `[](C_Foo& _ir_row_Foo, ...)` lambda. This is the engine's default
  tick signature (engine/system/CLAUDE.md "Three valid TICK function
  signatures") and the only one compatible with `PARALLEL_FOR` — the
  `isBatchForm_` validator path that would otherwise FATAL is now
  unreachable from CODEGEN. Bodies that don't fit the canonical
  per-row shape (multiple top-level statements, a non-canonical loop
  bound, `arch.length` outside the loop bound, a column-op index
  that isn't the loop variable) are rejected at codegen time with a
  message pointing at the requirement; the EVAL path remains
  available via `mode = "eval"` for bodies that need a different
  shape.

  A `local a = arch.Comp:at(i)` binding that is only read — never
  reassigned, and never read after the same column is written via
  `setAt`/`setField` — lowers to a `const auto&` alias of the row
  (`const auto& a = _ir_row_Comp;`) instead of a by-value copy, so
  read-light kernels skip the per-row struct copy (#1353). Any
  binding the emitter can't prove read-only stays by-value: the DSL
  forbids `a.field = ...`, so the only way the row changes mid-body
  is a `setAt`/`setField` on the same component, and a copy vs an
  alias differ only when a read is sequenced after such a write.

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
    IRSystem::PROPAGATE_TRANSFORM,
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
    components = { IRComponent.C_LocalTransform },
    tick = function(arch) ... end,
})

IRSystem.registerPipeline(IRTime.UPDATE, {
    IRSystem.systemId(SystemName.PROPAGATE_TRANSFORM),
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
  **Replaces** `event`'s whole system list (so does
  `registerPipelineGroups`).
- **`IRSystem.appendSystem(event, sysId)`** (#1540) — adds one system to
  the END of `event`'s **already-registered** pipeline as its own serial
  group, WITHOUT replacing the systems already there. The supported path
  when the C++ pipeline is built before the script runs (e.g. the midi
  runtime's `initSystems()` runs before `main.lua`) and Lua wants to add
  one UPDATE / RENDER system — `registerPipeline` would wipe the C++
  systems; `appendSystem` composes. Asserts in debug if the system is
  already in the pipeline (double-add → ticks twice in release).
- **`IRSystem.insertSystemBefore(event, sysId, anchor)`** /
  **`IRSystem.insertSystemAfter(event, sysId, anchor)`** (#1540) — the
  position-aware variants; insert `sysId` as its own serial group
  immediately before / after `anchor` (a SystemId already in `event`'s
  pipeline). Asserts in debug if `anchor` isn't in the pipeline; in
  release a bad anchor silently front-inserts.
  Underlying semantics: `engine/system/CLAUDE.md` "Appending to a live
  pipeline".
- **`IRSystem.setSystemCadence(sysId, n)`** / **`getSystemCadence(sysId)`**
  (#2404) — throttle a system to run 1-in-`n` phase ticks (n ≥ 1; 1 = every
  tick). Off-cadence ticks skip its entire dispatch. Paired with
  **`setSystemCadenceOffset(sysId, o)`** / **`getSystemCadenceOffset(sysId)`**
  — an initial phase stagger (`0..n-1`) so sibling throttled systems don't
  spike on the same tick. A throttled Lua system that integrates at the
  reduced rate reads **`getAccumulatedTicks(sysId)`** (phase ticks this run
  covers) or **`accumulatedDeltaTime(sysId)`** (that × the fixed UPDATE dt,
  UPDATE-phase-only) to stay numerically correct. `sysId` is any SystemId
  (from `IRSystem.systemId` or `IRSystem.registerSystem`). Underlying
  semantics: `engine/system/CLAUDE.md` "Per-system update cadence".
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

IRModifier.registerField("Movement.speed")           -- → FieldBindingId (scalar)
IRModifier.registerFieldVec3("Idle.bobOffset")       -- → FieldBindingId (vec3)
IRModifier.registerFieldQuat("Shake.rotation")       -- → FieldBindingId (quat)
IRModifier.fieldId("Movement.speed")                 -- → FieldBindingId
IRModifier.fieldName(id)                             -- → string | nil

IRModifier.add(entity, fieldNameOrId, {
    transform = IRModifier.Transform.ADD,
    value = 0.5,
    source = sourceEntity,           -- optional
    ticks = 60,                      -- optional, -1 = no decay
})
IRModifier.addVec3(entity, fieldNameOrId, {
    transform = IRModifier.Transform.ADD,
    value = { x = 0, y = 0.25, z = 0 },  -- {x,y,z} table or IRMath::vec3 userdata
    source = sourceEntity,
    ticks = -1,
})
IRModifier.addQuat(entity, fieldNameOrId, {
    transform = IRModifier.Transform.MULTIPLY,  -- MULTIPLY | OVERRIDE | SET
    value = { x = 0, y = 0, z = 0.1736, w = 0.9848 }, -- {x,y,z,w} or IRMath::vec4
    source = sourceEntity,
    ticks = -1,
})
IRModifier.addGlobal(fieldNameOrId, opts)
IRModifier.addGlobalVec3(fieldNameOrId, opts)
IRModifier.addGlobalQuat(fieldNameOrId, opts)
IRModifier.addLambda(entity, fieldNameOrId, fn, opts)
IRModifier.removeBySource(sourceEntity)

IRModifier.applyToField(entity, fieldNameOrId, base) -- → resolved float
IRModifier.applyToFieldVec3(entity, fieldNameOrId, base) -- → resolved vec3
IRModifier.applyToFieldQuat(entity, fieldNameOrId, base) -- → resolved vec4 quat
IRModifier.resolved(entity, fieldNameOrId, fallback) -- read C_ResolvedFields
IRModifier.resolvedVec3(entity, fieldNameOrId, fallback) -- read vec3 slot
IRModifier.resolvedQuat(entity, fieldNameOrId, fallback) -- read quat slot
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
- **Typed fields: scalar vs vec3 vs quat.** `registerField` declares a
  scalar field; `registerFieldVec3` declares a vec3 field;
  `registerFieldQuat` declares a quaternion field. Pushing the wrong
  type against a typed field silently no-ops — caller bug, not a data
  error. Resolved values live in three parallel slots on
  `C_ResolvedFields` (`fields_`, `fieldsVec3_`, `fieldsQuat_`) so the
  same name across types cannot cross-stream. `addLambda` is scalar-only
  in v1; vec3 / quat lambda channels are a Phase 2 follow-up.
- **Quat compose semantics.** `MULTIPLY` is **left-multiply / post-rotate**
  (`resolved = mod * base`), so stacked MULTIPLYs apply outer-first in
  push-order: for `[r1, r2, r3]`, `resolved = r3 * r2 * r1 * base`.
  `OVERRIDE` discards prior ops; `SET` replaces in push-order. The
  unit-quaternion-only `ADD` / `CLAMP_MIN` / `CLAMP_MAX` raise an
  engine assertion in debug and silently no-op in release. The compose
  pass normalizes the final resolved quat once at the end.

## Collision overlap events (`IRCollision.*`)

`bindLuaDrivenEcs()` exposes a Lua-facing overlap/trigger callback surface
(engine #1817) over the existing AABB collider + `C_ContactEvent` detection.
The binding lives in `lua_collision_bindings.hpp`
(`detail::bindCollisionEvents`); the dispatch + batched-pair plumbing is
`DISPATCH_LUA_OVERLAP` + `C_OverlapContactBatch` (see
`engine/prefabs/irreden/update/CLAUDE.md`).

```lua
-- Handlers keyed by an (unordered) collision-LAYER pair. The entity on the
-- FIRST layer arg is the callback's first arg; the second layer's entity is
-- second (so a creation never re-checks layers in Lua).
IRCollision.onOverlapEnter(layerA, layerB, function(entA, entB) ... end)
IRCollision.onOverlapExit (layerA, layerB, function(entA, entB) ... end)

-- Named engine layers as an integer table (cpp-lua-enums.md; keys derive
-- from the CollisionLayerMask enum so they can't drift):
IRCollision.Layer.COLLISION_LAYER_DEFAULT       -- 1
IRCollision.Layer.COLLISION_LAYER_NOTE_BLOCK    -- 2
IRCollision.Layer.COLLISION_LAYER_NOTE_PLATFORM -- 4
IRCollision.Layer.COLLISION_LAYER_PARTICLE      -- 8
```

- **Layer-pair keyed, not per-entity.** Handlers live for the session in
  `DISPATCH_LUA_OVERLAP`'s state, keyed by the layer pair — so a dead
  entity simply produces no more contacts (no dangling-handle lifetime
  problem). Enter/exit is derived at PAIR granularity, so one entity touching
  several others is tracked correctly.
- **Raw integer layers are accepted.** The four named engine layers are
  music-demo-centric; gameplay layers (player / enemy / projectile / pickup)
  are plain integer bits a creation defines — the dispatcher keys on the
  value, not the name. Pass either an `IRCollision.Layer.*` value or a raw
  bit. (Same-layer handlers — `onOverlapEnter(L, L, fn)` — pass the canonical
  `(a, b)` order.)
- **Wiring contract.** A creation must
  `registerPrefabSystem<IRSystem::DISPATCH_LUA_OVERLAP>()` and place it
  in the UPDATE pipeline **after** `COLLISION_NOTE_PLATFORM` before registering
  handlers — `onOverlap*` resolve the system through the prefab-system-id map
  (like `IRSystem.systemId`) and raise a Lua error naming the missing
  registration if it's absent. Registering the system also creates the
  `C_OverlapContactBatch` singleton, which is what switches pair emission on in
  the producer.
- **No physics response** — overlap notification only. The canonical runnable
  example (spawn collidables, compose the pipeline, register handlers, prove
  enter+exit from a headless run) is `creations/demos/collision_overlap_demo`.

## Sim clock (`IRSim.*`)

`bindLuaDrivenEcs()` exposes the sim-clock / cycle / timer / stopwatch
service (engine #200, `engine/prefabs/irreden/common/sim_clock.hpp`) as the
`IRSim` table. The **sim tick** is pausable and scalable (distinct from the
always-advancing engine tick); a creation composes `SIM_CLOCK_ADVANCE` /
`CYCLE_BOUNDARY_DETECT` / `TIMER_FIRE` into its UPDATE pipeline C++-side.

```lua
IRSim.setTimeScale(0.5)            -- 0 = paused, N = fast, 1/N = slow
IRSim.tick()                       -- current sim tick
IRSim.pause(); IRSim.resume(); IRSim.isPaused()

IRSim.createCycle("day", 24000)    -- name, periodTicks[, phaseOffset]
local t = IRSim.cycleFraction("day")          -- [0,1) — drive sun/colour on this
IRSim.cycleNumber("day"); IRSim.cycleTickWithin("day")
IRSim.cycleBoundaryCrossed("day")             -- true the tick a boundary crossed

IRSim.createTimer("reload", 50)    -- name, targetTick[, intervalTicks] (0 = one-shot)
IRSim.timerFired("reload"); IRSim.timerActive("reload")
IRSim.timerTicksRemaining("reload"); IRSim.timerFraction("reload")

IRSim.createStopwatch("run")
IRSim.stopwatchElapsed("run")
IRSim.stopwatchPause("run"); IRSim.stopwatchResume("run"); IRSim.stopwatchReset("run")
```

- **Discrete events are polled, not callbacks.** `cycleBoundaryCrossed` /
  `timerFired` read the embedded event flag (true only on the firing tick)
  the same tick the C++ detector raised it — the events-as-components
  model. A registered-callback (`onCycleBoundary`) form is a follow-up if a
  real consumer needs it.
- **Names are registry keys**, not enums — string lookups here are allowed
  (cf. modifier `fieldNameOrId`); a handful of cycles/timers per world makes
  the linear name scan a query convenience, not a hot path.
- Service surface only — the advance systems are not exposed for Lua
  pipeline assembly (same as most prefab systems). Coverage:
  `test/time/lua_sim_test.cpp`.

## Persistence (`IRSave.*`)

`bindLuaDrivenEcs()` exposes a flat key/value persistence surface as the
`IRSave` table (engine #1819) so gameplay can save high scores + settings
across launches — the lightweight, Lua-reachable alternative to the ECS
world snapshot (#199). It wraps the `.irkv` format in `engine/asset/`
(`key_value_store.hpp`); see `engine/asset/CLAUDE.md` "`.irkv` format" for
the on-disk layout and value model.

```lua
IRSave.load("highscores")              -- read file into the in-proc store;
                                       -- missing/corrupt -> empty. returns loaded?
IRSave.set("highscores", "top1", 5000) -- number | string | bool | array-table
IRSave.set("highscores", "names", { "ACE", "BEE" })
IRSave.save("highscores")              -- write the in-proc store to disk. returns ok?
local v = IRSave.get("highscores", "top1", 0)  -- stored value, or the default arg
IRSave.has("highscores", "top1"); IRSave.remove("highscores", "top1"); IRSave.clear("highscores")
```

- **Stores are namespaced by basename** (`"highscores"`, `"settings"`); each
  maps to `userDataDir("irreden")/<name>.irkv` (per-user dir, not the exe
  dir — survives a clean bundle reinstall). `save` creates the directory.
- **Per-`World` lifetime.** The store registry is captured in the binding
  lambdas (shared_ptr), so it lives as long as the `LuaScript`'s sol::state.
  Don't hold Lua handles across `World` shutdown.
- **Values cross by `sol::type` inspection** (number/string/bool/array-table),
  NOT a Lua-spelled enum, so the `cpp-lua-enums` rule does not apply. A
  number/string/bool maps to a scalar; an array-style table (contiguous
  `1..n`) maps to a LIST. A map-style table or a nested list raises a clear
  Lua error rather than silently dropping the value. Lists do not nest in v1.
- Coverage: `test/script/lua_persistence_test.cpp` (in-memory surface) +
  `test/asset/key_value_store_test.cpp` (format round-trip + corruption).

## World snapshot (`IRPersist.*`)

`bindLuaDrivenEcs()` also exposes the ECS **world snapshot** (persist P7, #2218,
epic #667) as the `IRPersist` table — whole-world binary save/load, distinct
from `IRSave` (the flat KV store above). `lua_world_snapshot_bindings.hpp`'s
`bindWorldSnapshotApi` is a thin forward to
`IRWorld::saveWorld/loadWorld(path)` over `makeDefaultSaveRegistry()`. Because
that inline glue *calls* those symbols — not just includes the header, as the
render/audio glue does — Scripting **links** `IrredenEngineWorld`. World links
Scripting back, so the edge closes a static-library cycle; CMake resolves it by
repeating both archives on the final link line, which is what lets single-pass
linkers (mingw / GNU ld) rescan World after Scripting's back-reference. Without
the link those symbols went undefined on native-Windows demo links; Apple
ld64's multi-pass resolver had masked the gap (#2499).

```lua
IRPersist.saveWorld("scene.irws")   -- serialize the live world (+ .json sidecar).
                                    -- returns ok? (false on I/O failure, logged)
IRWorld.resetGameplay()             -- clear the world at a frame boundary FIRST
IRPersist.loadWorld("scene.irws")   -- restore into the cleared world. returns ok?
```

- **`path` is a raw filesystem path** (world snapshots are dev/tool artifacts),
  NOT routed through `userDataDir` like `IRSave`.
- **Frame-boundary contract (documented, not enforced — mirrors
  `IRWorld.resetGameplay`).** Both calls touch the whole archetype graph, so
  call them from a startup script / command handler / scene-transition step,
  **never** from a Lua system tick or `DISPATCH_LUA_*` callback. `loadWorld`
  does NOT reset first — call `IRWorld.resetGameplay()` immediately before it,
  or the restore aborts on an id collision and returns `false`.
- **Coverage subset.** The default registry persists only components with a
  landed `SaveSerialize<C>` (`C_VoxelSetNew` + a few PODs today); see
  `engine/world/CLAUDE.md` "Process-default registry". A `.json.txt` debug dump
  is available behind the `IR_PERSIST_DUMP` env flag.
- **Errors:** an I/O failure returns `false` (logged); a non-string argument
  raises a Lua error via sol coercion (the same split `IRSave` uses).
- Coverage: `test/script/lua_world_snapshot_test.cpp`.

## Prefab format (`Prefab.register`, `Prefab.spawn`)

`bindLuaDrivenEcs()` also exposes the `Prefab` Lua table — the runtime
half of Phase 5 of the editor epic (#608). A prefab is a Lua file that
returns a table describing an entity template; `Prefab.spawn(id, pos)`
loads the file and reconstitutes the entity. The schema is versioned
from day one so the editor's component-palette emissions stay
forward-compatible.

v1 schema:

```lua
return {
    prefab_version = 1,                  -- REQUIRED, must equal 1
    voxel_ref      = "creations/...vxs", -- OPTIONAL, loaded via loadVoxelSet
    rig_ref        = "creations/...rig", -- OPTIONAL, loaded via loadRig
    rotation_mode  = IRComponent.RotationMode.GRID
                  | IRComponent.RotationMode.DETACHED
                  | IRComponent.RotationMode.DETACHED_REVOXELIZE,
                                         -- OPTIONAL, default GRID
    unbounded      = true | false,       -- OPTIONAL, default false
    canvas_size    = { x = 64, y = 64 }, -- REQUIRED when DETACHED or DETACHED_REVOXELIZE
    setup          = function(entity)    -- OPTIONAL, user-provided
        IREntity.setComponent(entity, ...)
    end,
}
```

`rotation_mode` (Epic C C2) attaches `C_RotationMode` to the spawned
root. GRID (default) renders into the world voxel pool with grid-
quantized rotation; DETACHED allocates a per-entity `C_EntityCanvas`
via `IRPrefab::EntityCanvas::create()` so a future C3 composite pass
threads the entity's `C_LocalTransform` through the per-canvas TRS
without per-voxel rebake; DETACHED_REVOXELIZE extends DETACHED by
re-filling the canvas pool at full-rotation cell positions each frame
so asymmetric solids read as true 3D-rotated (not 2D-warped) shapes.
`canvas_size = { x, y }` is required for DETACHED and DETACHED_REVOXELIZE
— sized in trixels — and is ignored for GRID. `unbounded =
true` sets `C_LocalTransform::unbounded_` for sub-trixel positioning;
meaningful with DETACHED and DETACHED_REVOXELIZE.

**The schema accepts the `IRComponent.RotationMode.{GRID,DETACHED,DETACHED_REVOXELIZE}`
enum value (an integer), not a string.** String-name lookups —
`rotation_mode = 'GRID'` — surface a schema error with the
`IRComponent.RotationMode.X` spelling in the diagnostic. This keeps
the Lua-side authoring surface in lockstep with the C++ enum, the
same way `IRModifier.Transform.X`, `IRTime.X`, `IRSystem.SystemName.X`,
`IRCommand.CommandName.X` already work. See
[`.claude/rules/cpp-lua-enums.md`](../../.claude/rules/cpp-lua-enums.md)
for the general rule.

Out-of-range integers, missing `canvas_size`, or non-positive
width/height also surface a schema error and the spawn fails
cleanly.

C++ surface lives at `engine/script/include/irreden/script/prefab_api.hpp`
in `namespace IRPrefab::Prefab`:

- `registerPrefab(id, path)` / `prefabPath(id)` / `clearPrefabs()` — the
  in-process registry. Process-singleton lifetime; `clearPrefabs()` is
  test-only.
- `spawnPrefab(LuaScript&, id, position) → SpawnResult` — load + execute.

Lua side:

- `Prefab.register("ant", "creations/foo/ant.prefab.lua")` — register.
- `Prefab.spawn("ant", {x=0, y=0, z=0})` — spawn. Returns a `LuaEntity`
  on success and `(nil, errorMessage)` on failure.

**Schema-validation behaviors:**

- Missing `prefab_version` → error (`prefab_version field missing or
  not an integer`).
- `prefab_version != 1` → error with the unsupported version surfaced.
- Non-table return from the prefab file → error.
- `voxel_ref` that fails to load (bad magic, truncated, missing file)
  → error; no entity created.
- `rig_ref` that fails to load → error.
- `setup` present but not a function (e.g. `setup = 42`) → error;
  catches the typo class instead of silently no-op'ing.
- Unknown top-level keys are silently ignored — adding a future field
  doesn't break older loaders (Rule #1 mirror).

**v1 scope notes** — three follow-up areas the editor will eventually
land:

- **Declarative `components = { C_Name = { ... } }` table** — landed via
  #698. The optional `components` table maps each component's binding
  string (the name passed to `registerType<T>("C_Foo")`) to a table of
  field overrides; spawn() runs each entry's registered factory after
  rig / voxel attachment and before `setup`, so a `setup` callback
  observes (and may freely overwrite) declaratively-attached
  components.

  ```lua
  return {
      prefab_version = 1,
      components = {
          C_ZoomLevel = { zoom = 5.0 },
          -- ... any component whose *_lua.hpp opted in ...
      },
  }
  ```

  Wiring contract: each `*_lua.hpp` opts in by calling
  `IRScript::registerComponentFactoryFor<C>(name, setFields)` after the
  usertype registration. `setFields(C&, const sol::table&)` copies
  override values out of the table into the default-constructed
  component; the factory then attaches via `IREntity::setComponent`.
  See `engine/prefabs/irreden/render/components/component_zoom_level_lua.hpp`
  for the canonical opt-in shape.

  Error modes (each destroys the partial entity + any spawned children
  before returning):
  - Top-level `components` value is not a table (e.g. `components = 42`)
    → silently treated as absent. `sol::optional<sol::table>` returns
    `nullopt` and the block is skipped, just like an omitted field.
  - Per-entry value is not a table (e.g.
    `components = { C_ZoomLevel = 42 }`) → "components['<name>'] must
    be a table of field overrides".
  - Component name has no registered factory → "no factory registered
    for component '<name>'" with the binding-fix hint pointing at
    `IRScript::registerComponentFactoryFor`.

  v1 carry-over: components without a `*_lua.hpp` (and thus no
  factory) remain unreachable from the declarative table — the `setup`
  callback is still the escape hatch for those.
- **`bind_point_overrides = { ... }`** — landed via T-181. The
  optional `bind_point_overrides` table on a prefab maps names to
  `{ offset = vec3, rotation = vec4, boneId = uint }` (any subset);
  spawn() merges the override into the rig's BIND-chunk entry,
  overwriting only the fields the override specifies. Unknown names
  are inserted as fresh bind points so a prefab can extend a rig's
  defaults without re-saving the asset.

  At spawn time, `Prefab.spawn` translates the rig's BIND chunk into
  a runtime `IRComponents::C_BindPoints` via
  `IRPrefab::Rig::toBindPoints` and attaches it alongside the
  `C_JointHierarchy`. Bind-point world transforms are queried via
  `IREntity.bindPoint(entity, name)` on the `IREntity` table:

  ```lua
  -- Returns (offset, rotation) as vec3 + vec4 in entity-local
  -- world space (composed from the joint chain). Returns nil, nil
  -- if the entity has no C_BindPoints or the named point is absent.
  local offset, rotation = IREntity.bindPoint(entity, "right_hand")
  ```

  The composition algorithm walks the bone chain root-first via the
  `C_JointHierarchy.joints_[].parentIndex_` field, accumulating
  per-joint local rotations (quaternion multiply, see
  `IRMath::quatMul`) and translations (rotated by the chain
  rotation, see `IRMath::rotateVectorByQuat`), then applies the
  bind point's local offset/rotation on top. If the entity has no
  `C_JointHierarchy` component, the bind point's local transform
  is returned as-is.

  Cost contract: `IREntity.bindPoint` performs an
  `unordered_map<string, ...>` lookup plus an O(chainDepth) walk.
  Treat as a **one-time query at spawn or on interaction**, not per
  tick. If a hot use case appears (e.g. attaching a per-tick light
  to a bind point), pre-resolve the integer bone index at spawn
  time and cache the offset/rotation in a flat component instead.
- **`C_VoxelSetNew` canvas-attach pass for staged dense data** —
  DENSE records do attach as `C_VoxelSetNew` on the spawned entity
  (see "DENSE voxel_ref attachment" below), but when the prefab
  spawns before a render canvas exists the per-voxel records live in
  `pendingVoxels_` and no pool allocation happens. A follow-up pass
  needs to seed the pool the frame after a canvas activates and
  promote the staged records into the pool span — track in the issue
  queue.

**SHAPES voxel_ref attachment (effects-only per D2, Epic D #937).**
When `voxel_ref` points at a SHAPES or HYBRID `.vxs`, `Prefab.spawn`
attaches one child entity per `ShapeRecord` under the spawned root
via `IREntity::setParent`. SHAPES is reserved for effects-only SDF
entities (occluders, auras, soft glows); HYBRID is deprecated for new
authoring but loads backward-compatibly.

Each child carries:

- `C_LocalTransform{record.offset_}` — record-local position;
  `PROPAGATE_TRANSFORM` walks the CHILD_OF chain and composes it
  with the parent's transform into the child's `C_WorldTransform`.
- `C_ShapeDescriptor{shapeType, params, color}` with `flags_`
  copied from the record. The SDF primitive renders through the
  normal `SHAPES_TO_TRIXEL` pass once a canvas is active.

`ShapeRecord::rotation_`, `csgOp_`, and `boneId_` are loaded from
disk but not stamped onto runtime components in v1 — the current
renderer doesn't consume them. T-181 wires bone bindings; CSG
composition is a render-pipeline design call.

`C_ShapeDescriptor`'s default + 3-arg constructors snapshot the
active canvas via `IRRender::getActiveCanvasEntityOrNull()` so
headless contexts (tests, asset-only tooling) construct descriptors
with `canvasEntity_ = kNullEntity` instead of asserting on the
absent `RenderManager`. Iterating render systems already gate work
on a non-null canvas binding.

**DENSE voxel_ref attachment (primary entity path; D2, Epic D #937).**
When `voxel_ref` points at a DENSE `.vxs` (or a HYBRID file loading
for backward-compat), `Prefab.spawn` attaches a `C_VoxelSetNew` on
the spawned root entity via the bridge
`IRPrefab::DenseVoxel::toComponent` in
`engine/prefabs/irreden/voxel/dense_bridge.hpp`. The bridge calls
the dense-data ctor `C_VoxelSetNew(boundsMin, boundsMax,
span<const C_Voxel>)`, which is headless-safe:

- **Active canvas** → allocates from the pool, copies records
  into the pool span, seeds per-voxel positions at
  `boundsMin + index3D`. `numVoxels_` reflects the allocation.
- **Headless** (test / pre-canvas) → stages records in
  `pendingVoxels_`, records `pendingBoundsMin_`, leaves
  `numVoxels_ = 0` and `canvasEntity_ = kNullEntity`. The pool
  is not touched.

`C_VoxelSetNew::recordCount()` returns the data count regardless
of mode, so the round-trip test asserts
`recordCount() == dense.voxelCount()` without caring whether the
pool was reachable. For HYBRID files loading in backward-compat
mode, the SHAPES half attaches as child entities (per "SHAPES
voxel_ref attachment") and the DENSE half attaches to the root in
the same spawn call. New prefabs should not rely on this dual-half
path — HYBRID authoring is deprecated per D2.

If the loaded `.vxs` is malformed (e.g. BNDS present but VOXR
truncated, so `dense.voxels_.size() != dense.voxelCount()`), the
bridge returns an empty component and `setComponent` is skipped
— Save Format Extensibility Rule #5: unknown is recoverable.

## Script resolution

`scriptFile(path)` passes the path straight to sol2 / `dofile`. Behavior:

- Bare filename → resolved from cwd (which is the exe's runtime dir at
  launch time; see [`docs/agents/BUILD.md`](../../docs/agents/BUILD.md) "Running an executable").
- Path with a directory separator → resolved from cwd.
- Absolute path → used as-is.

There is no sandbox, no path-traversal check, and no archive support.
Creations ship `.lua` files in `creations/<name>/scripts/` and a top-level
`main.lua`.

## Commands and input (`IRCommand.*`, `IRInput.*`)

`LuaScript::bindLuaCommands()` wires the engine's `IRCommand` /
`IRInput` surface into Lua so a creation can declare commands and
bind keyboard/mouse/gamepad inputs without a C++ `initCommands()`
block. The locked design lives in
[`docs/design/lua-input-commands.md`](../../docs/design/lua-input-commands.md);
`creations/demos/default/commands.lua` is the canonical migration
example.

```lua
local CN = IRCommand.CommandName
local IT = IRInput.InputType
local BS = IRInput.ButtonStatus
local K  = IRInput.Key
local CTRL = IRInput.Modifier.CONTROL

-- Bind a prefab command (Command<NAME>::create() body) to an input
-- trigger. Returns a CommandId.
IRCommand.bindPrefab(CN.CLOSE_WINDOW, IT.KEY_MOUSE, BS.PRESSED, K.ESCAPE)

-- Compose modifiers with LuaJIT's native bit.bor (no IRInput.modMask
-- wrapper):
IRCommand.bindPrefab(
    CN.GUI_ZOOM_IN, IT.KEY_MOUSE, BS.PRESSED, K.EQUAL,
    bit.bor(CTRL, IRInput.Modifier.SHIFT)
)

-- Lua-defined command body (returns a CommandId for IRCommand.fire):
local pulseId = IRCommand.createCommand(
    IT.KEY_MOUSE, BS.PRESSED, K.P,
    function()
        IRModifier.addGlobal("GameSpeed.scale", {
            transform = IRModifier.Transform.MULTIPLY,
            value = 0.5, ticks = 120,
        })
    end
)

-- Fire imperatively from any Lua context:
IRCommand.fire(pulseId)
IRCommand.fireByName(CN.SCREENSHOT)
```

**API**

- `IRCommand.bindPrefab(commandName, inputType, status, button,
  requiredMods?, blockedMods?) -> CommandId` — register a prefab
  `Command<NAME>::create()` body against an input trigger. Returns
  `CommandId` (`uint32_t`). For enum values without a `Command<NAME>`
  specialization (`NULL_COMMAND`, `EXAMPLE`, `SET_TRIXEL_COLOR`,
  ...), returns `kInvalidCommandId` and logs an error.
- `IRCommand.createCommand(inputType, status, button, fn,
  requiredMods?, blockedMods?) -> CommandId` — register a Lua closure
  body. Body errors are caught in-VM via `sol::protected_function`
  and logged; the dispatch loop continues.
- `IRCommand.fire(commandId)` — invoke a registered command id
  imperatively (bypasses input trigger). Bounds-checked; out-of-range
  ids log + return.
- `IRCommand.fireByName(commandName)` — invoke a prefab command's
  body without registering it first.

**Enum tables.** All integer tables; spell every value through them
in Lua (never bare integer literals).

- `IRCommand.CommandName.{CLOSE_WINDOW, ZOOM_IN, ...}` — hand-listed
  against `IRCommand::CommandNames` in
  `engine/command/include/irreden/command/ir_command_types.hpp`.
  Adding a new prefab command requires appending an `IR_BIND_CMD`
  line in `engine/script/include/irreden/script/lua_command_bindings.hpp`
  AND a case in `fireByName` / `bindPrefabCommand` in
  `engine/command/src/ir_command.cpp`.
- `IRInput.InputType.{KEY_MOUSE, GAMEPAD, MIDI_NOTE, MIDI_CC}`
- `IRInput.ButtonStatus.{NOT_HELD, PRESSED, HELD, RELEASED,
  PRESSED_AND_RELEASED}`
- `IRInput.Key.{A..Z, NUM_0..NUM_9, F1..F12, SPACE, ENTER, TAB,
  BACKSPACE, ESCAPE, INSERT, DELETE, HOME, END, PAGE_UP, PAGE_DOWN,
  UP, DOWN, LEFT, RIGHT, LEFT_SHIFT/CONTROL/ALT, RIGHT_SHIFT/CONTROL/
  ALT, MINUS, EQUAL, COMMA, PERIOD, SLASH, SEMICOLON, APOSTROPHE,
  LEFT_BRACKET, RIGHT_BRACKET, BACKSLASH, GRAVE, CAPS_LOCK,
  MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE}`. Drops the engine-internal
  `kKeyButton` / `kMouseButton` prefixes for readability.
- `IRInput.Modifier.{NONE, SHIFT, CONTROL, ALT}` — bitmask bits.
  Compose with LuaJIT `bit.bor(a, b, ...)`.
- `IRInput.GamepadButton.{A, B, X, Y, LEFT_BUMPER, RIGHT_BUMPER,
  BACK, START, GUIDE, LEFT_THUMB, RIGHT_THUMB, D_PAD_UP/RIGHT/DOWN/
  LEFT}`
- `IRInput.GamepadAxis.{LEFT_X, LEFT_Y, RIGHT_X, RIGHT_Y,
  LEFT_TRIGGER, RIGHT_TRIGGER}`

**Wiring.** Call `LuaScript::bindLuaCommands()` once during creation
init, typically right after `bindLuaDrivenEcs()`. A creation that
doesn't use Lua-driven ECS may call `bindLuaCommands()` standalone.
The detail helpers (`bindCommandNameEnum`, `bindInputEnums`,
`bindCommandFunctions`) are idempotent — second calls are no-ops
that preserve the existing Lua handles.

**Out of scope for the v1 surface** (see the design doc § "Out of
scope" for the full list): hot-reload of Lua command bodies
(`replaceCommandBody`), runtime unbind (`unbind` /
`unbindAll`), `IRInput.modMask(...)` (use `bit.bor`), MIDI command
bindings (`registerMidiNoteCommand` / `registerMidiCCCommand`),
gamepad-axis-to-value bindings, IRInput query helpers
(`checkKeyMouseButton` etc.).

## Render glue + GUI draw (`IRRender.*`, `IRGui.*`)

`bindLuaDrivenEcs()` exposes a shared render-glue surface (engine #1615) so
creations on the Lua-first authoring path drive lighting and HUD visuals
without re-declaring per-creation pass-throughs. The binding block lives in
`engine/script/include/irreden/script/lua_render_bindings.hpp`
(`detail::bindRenderGlue`).

```lua
-- Lighting (thin pass-throughs to the existing IRRender:: setters):
IRRender.setSunDirection(x, y, z)  -- forwards to IRRender::setSunDirection;
                                   -- the engine-side `z <= 0` assert + normalize
                                   -- on write are preserved
IRRender.setSunIntensity(f)
IRRender.setSunAmbient(f)
IRRender.setSkyColor(r, g, b)      -- sky-hemisphere term (0..1 floats)
IRRender.setSkyIntensity(f)

-- GUI-canvas shape draw (screen-space trixel coords on the "gui" canvas):
IRGui.drawDisc(x, y, radius, { 255, 64, 64 })          -- filled disc
IRGui.drawLine(x0, y0, x1, y1, { r = 0, g = 255, b = 0 }) -- 1-trixel line
```

- **No clear/background-color setter exists engine-side** — `setSkyColor`
  (the additive sky-hemisphere term consumed by the HDR composite) is the
  closest color knob. If a true clear-color setter lands on `IRRender::`,
  expose it here as a follow-up.
- **Color argument** — an `IRMath::Color` userdata or a `{ r, g, b[, a] }` /
  `{ 1, 2, 3[, 4] }` table (0-255 components, keyed or indexed; missing
  channels default to 255). Parsed by `IRScript::colorFromLua` (see the
  helper table below).
- **The shape draws forward to the `IRRender::drawGuiDisc` / `drawGuiLine`
  entry points**, which own the "gui"-canvas resolution and rasterize via the
  same `subImage2D` path the C++ widget render systems use
  (`engine/render/include/irreden/render/trixel_rect.hpp` `fillDisc` /
  `drawLine`). They are IMMEDIATE-MODE: the GUI canvas is cleared every frame
  by `TEXT_TO_TRIXEL`, so a draw persists on screen ONLY if re-issued each
  frame from a RENDER-phase Lua system positioned after that clear — the same
  contract the widget render systems follow. A one-shot draw at script-load
  time is wiped on the next frame that clears the canvas. (Writing via
  `subImage2D` keeps the draw correctly ordered against the clear in
  command-buffer order on Metal — see `engine/render/CLAUDE.md` "CPU texture
  writes order via the command buffer on Metal".)
- `IRRender` / `IRGui` tables are created with the
  `if (!valid())` guard, so a creation that also populates its own `IRRender`
  table (e.g. the default demo's `getGuiScale` / `measureText`) keeps those
  entries — the glue extends, never replaces.

Coverage: `test/script/lua_render_bindings_test.cpp` asserts table/function
presence + the `colorFromLua` parse cases headless; the runtime draw + sun
path is exercised visually by `creations/demos/lua_pipeline_demo`.

## Widget framework bindings (`IRGui.make*`, `WIDGET_LUA_DISPATCH`)

`bindLuaDrivenEcs()` also extends the `IRGui` / `IRRender` tables with the
widget-framework surface (engine #1975) so a creation builds a panel + label +
button **entirely from Lua**, with a Lua `onClick` that fires on click. The
binding block is `detail::bindWidgets` in
`engine/script/include/irreden/script/lua_widget_bindings.hpp`; it forwards to
the C++ `IRPrefab::Widget::make*` constructors (which take `IRMath::ivec2`, so
the binding composes the ivec2s from plain Lua ints).

```lua
local panel  = IRGui.makePanel(x, y, w, h, title?, drawBorder?, zOrder?)
local label  = IRGui.makeLabel(x, y, text, color?)   -- color: IRMath::Color or {r,g,b[,a]}
local button = IRGui.makeButton(x, y, w, h, label, onClick?)  -- onClick(widgetId)
if IRGui.wasClicked(button) then ... end             -- poll the click pulse
local gx, gy = IRGui.glyphStep()                      -- font cell step (px)
local w,  h  = IRRender.getGuiCanvasSize()            -- "gui" canvas size (trixels)
```

- **`onClick` dispatch (the load-bearing new piece).** Click was poll-only
  (`C_WidgetState::fireAction_` via `IRGui.wasClicked`) until this landed. A
  Lua `onClick` is stored in the `WIDGET_LUA_DISPATCH` system's session-lifetime
  state (NOT a component), keyed by the widget's `EntityId`; the system's tick
  invokes it (error-trapped) the frame `fireAction_` pulses, passing the
  clicked widget's id. Same prefab-system-id-map resolution + lifetime contract
  as `DISPATCH_LUA_OVERLAP` (`resolveWidgetDispatch` mirrors
  `resolveOverlapDispatch`).
- **Wiring contract.** A creation must
  `registerPrefabSystem<IRSystem::WIDGET_LUA_DISPATCH>()` and place that system
  in the INPUT pipeline **immediately after `WIDGET_INPUT`** (so `fireAction_`
  is fresh) before main.lua registers an `onClick` — the binding raises a Lua
  error naming the missing registration if it's absent. Because
  `registerPrefabSystem` and a C++ `createSystem` would make two instances, the
  creation passes the registered prefab id into the pipeline (so the binding
  and the ticked instance are the same one).
- **`IRGui` / `IRRender` are extended, never replaced** (`if (!valid())`
  guard), so the render-glue `drawDisc`/`drawLine` and setters bound earlier
  survive.
- **`getGuiCanvasSize` / `glyphStep`** are layout accessors so Lua lays out
  against the live GUI-canvas size + font metrics. `getGuiCanvasSize` raises a
  Lua error if no `"gui"` canvas exists.

Coverage: `creations/demos/lua_widgets` builds the whole UI from `main.lua` and
proves `onClick` + `wasClicked` headlessly via the gui-verify GUI-test harness
(`GUI-ASSERT … result=PASS`).

## C++ ↔ Lua math type helpers

When a binding accepts a math type that Lua callers may pass as either a
registered userdata or a table literal, use the canonical helpers in
`engine/script/include/irreden/script/ir_script_utils.hpp` rather than writing
ad-hoc extraction lambdas per binding:

| Task | Helper |
|------|--------|
| `sol::object` → `IRMath::vec3` | `IRScript::vec3FromLua(obj)` |
| `sol::object` → `IRMath::Color` | `IRScript::colorFromLua(obj)` |

`vec3FromLua` accepts an `IRMath::vec3` userdata **or** a `{x,y,z}` / `{1,2,3}`
table. Returns `{0,0,0}` for nil/none. Validate the type at the callsite and
return an error string *before* calling the helper — it zero-defaults on
unrecognized types so bad-type errors need a caller-side check.

To add a helper for a new math type, add an `inline` free function to
`ir_script_utils.hpp` and extend the table above.

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
- **Prefab registry is process-global.** `IRPrefab::Prefab` keeps a
  static `std::unordered_map<std::string, std::string>` of id→path. It
  survives `World` shutdown. Tests must call
  `IRPrefab::Prefab::clearPrefabs()` between fixtures to avoid id
  bleed. Production creations register prefabs once at init and don't
  need to clear.
