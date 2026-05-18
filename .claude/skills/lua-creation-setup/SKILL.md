---
name: lua-creation-setup
description: >-
  Set up Lua scripting for an Irreden Engine creation: bindings, component
  packs, config files, script wiring, and Lua-defined components/systems via
  the codegen (build-time C++) or EVAL (runtime LuaJIT) path. Use when the
  user wants to add Lua support to a creation, register Lua bindings, expose
  components to Lua, define new components/systems in Lua, or compose
  pipelines from Lua.
---

# Lua Creation Setup

LuaJIT 2.1 + sol2. C++ sets up systems and pipelines; Lua drives entity creation, configuration, and runtime game logic. Lua bindings must be registered **before** `IREngine::init`.

There are two distinct use cases, often combined:

1. **Exposing C++ types to Lua** — register engine components, enums, and entity-factory functions so Lua scripts can use them. This is the classic path covered by `lua_bindings.cpp` / `lua_component_pack.hpp`.
2. **Defining new components and systems in Lua** — declare new ECS component schemas and system tick bodies entirely in Lua. The engine generates typed C++ storage at build time (`CODEGEN` mode) or registers them dynamically at runtime (`EVAL` mode). Covered by the `IRComponent.register` / `IRSystem.registerSystem` APIs.

These two paths share one `ComponentId` space — a Lua-defined component and a C++ component live in the same archetype graph with the same contiguous SoA storage.

---

## Required Files

```
creations/<category>/<name>/
├── main_lua.cpp          # C++ entry: registerLuaBindings -> init -> loop
├── lua_bindings.hpp      # Declares registerLuaBindings()
├── lua_bindings.cpp      # Registers enums, types, components, API tables
├── lua_component_pack.hpp # Lists components for bulk Lua registration
├── config.lua            # Window, video, MIDI config (parsed at startup)
├── main.lua              # Main Lua entry; dofile() sub-scripts
└── scripts/              # Sub-scripts loaded by main.lua
```

If the creation uses Lua-defined components or systems (codegen path), the
`main.lua` also contains `IRComponent.register` / `IRSystem.registerSystem`
calls. `CMakeLists.txt` runs `irreden_lua_codegen()` on it at build time.

---

## Part A: Exposing C++ Types to Lua

### Step 1: lua_bindings.hpp

Declare the registration function in your creation's namespace:

```cpp
#ifndef CREATIONS_MYCREATION_LUA_BINDINGS_H
#define CREATIONS_MYCREATION_LUA_BINDINGS_H

namespace MyCreation {
void registerLuaBindings();
}

#endif
```

### Step 2: lua_bindings.cpp

Register types, enums, components, and API tables inside the lambda passed to `IREngine::registerLuaBindings`:

```cpp
#include "lua_bindings.hpp"
#include "lua_component_pack.hpp"

#include <irreden/ir_engine.hpp>
#include <irreden/ir_audio.hpp>

namespace MyCreation {
void registerLuaBindings() {
    static bool isRegistered = false;
    if (isRegistered) return;

    IREngine::registerLuaBindings([](IRScript::LuaScript &luaScript) {
        using namespace IRMath;
        using namespace IRComponents;

        // Register plain types
        luaScript.registerType<Color, Color(int, int, int, int)>(
            "Color", "r", &Color::red_, "g", &Color::green_,
            "b", &Color::blue_, "a", &Color::alpha_
        );
        luaScript.registerType<ivec3, ivec3(int, int, int)>(
            "ivec3", "x", &ivec3::x, "y", &ivec3::y, "z", &ivec3::z
        );
        luaScript.registerType<vec3, vec3(float, float, float)>(
            "vec3", "x", &vec3::x, "y", &vec3::y, "z", &vec3::z
        );

        // Register enums
        luaScript.registerEnum<IREasingFunctions>(
            "IREasingFunction",
            {{"LINEAR_INTERPOLATION", kLinearInterpolation},
             {"QUADRATIC_EASE_IN", kQuadraticEaseIn},
             // ... more entries ...
            }
        );

        // Bulk-register Lua-opted components
        registerLuaComponentPack(luaScript);

        // Entity creation functions
        luaScript.registerCreateEntityFunction<C_MidiSequence>(
            "createMidiSequence"
        );
        luaScript.registerCreateEntityBatchFunction<
            C_Position3D, C_VoxelSetNew, C_PeriodicIdle>(
            "createEntityBatchVoxelPeriodicIdle"
        );

        // API tables
        luaScript.lua()["IRAudio"] = luaScript.lua().create_table();
        luaScript.lua()["IRAudio"]["openMidiOut"] = [](const std::string &name) {
            return IRAudio::openPortMidiOut(name);
        };
    });

    isRegistered = true;
}
} // namespace MyCreation
```

#### Key registration methods

| Method | Purpose |
|--------|---------|
| `registerType<T, Constructors...>(name, key, &T::member, ...)` | Register a plain type with named fields |
| `registerEnum<E>(name, {{key, value}, ...})` | Register an enum as a Lua table |
| `registerTypesFromTraits<C_A, C_B, ...>()` | Bulk-register components that have `*_lua.hpp` traits |
| `registerTypeFromTraits<C_A>()` | Register one component with `*_lua.hpp` traits |
| `registerCreateEntityFunction<Components...>(name)` | Register an `IREntity.name(...)` Lua factory |
| `registerCreateEntityBatchFunction<Components...>(name)` | Register a batch factory for multiple entities |

### Step 3: lua_component_pack.hpp

Include the `*_lua.hpp` variant of each component and call `registerTypesFromTraits`:

```cpp
#ifndef CREATIONS_MYCREATION_LUA_COMPONENT_PACK_H
#define CREATIONS_MYCREATION_LUA_COMPONENT_PACK_H

#include <irreden/common/components/component_position_3d_lua.hpp>
#include <irreden/update/components/component_velocity_3d_lua.hpp>
#include <irreden/voxel/components/component_voxel_set_lua.hpp>
#include <irreden/audio/components/component_midi_note_lua.hpp>

namespace MyCreation {
inline void registerLuaComponentPack(IRScript::LuaScript &luaScript) {
    using namespace IRComponents;
    luaScript.registerTypesFromTraits<
        C_Position3D,
        C_Velocity3D,
        C_VoxelSetNew,
        C_MidiNote>();
}
}

#endif
```

### Opting a C++ Component into Lua

Create a `*_lua.hpp` file alongside the component. Two things needed:

1. Specialize `kHasLuaBinding<T> = true`
2. Implement `bindLuaType<T>(LuaScript&)`

```cpp
#ifndef COMPONENT_MY_THING_LUA_H
#define COMPONENT_MY_THING_LUA_H

#include <irreden/update/components/component_my_thing.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <>
inline constexpr bool kHasLuaBinding<IRComponents::C_MyThing> = true;

template <>
inline void bindLuaType<IRComponents::C_MyThing>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_MyThing,
        IRComponents::C_MyThing(float, float)>(
        "C_MyThing",
        "speed", &IRComponents::C_MyThing::speed_,
        "radius", &IRComponents::C_MyThing::radius_
    );
}

} // namespace IRScript

#endif
```

Then include this `*_lua.hpp` in your `lua_component_pack.hpp` and add the type to `registerTypesFromTraits<...>()`.

---

## Part B: Lua-defined Components and Systems (Codegen Path)

The codegen path lets you define entirely new ECS components and systems in Lua. The `cmake/lua_codegen` tool reads your `.lua` source at build time and emits a typed C++ struct + `IRSystem::createSystem<...>` specialisation per system — **CODEGEN mode**. The same `.lua` file is loaded at runtime, where `IRComponent.register` resolves to the pre-bound handle (idempotent) and `IRSystem.registerSystem` no-ops or registers depending on the mode.

Switching to **EVAL mode** (`-DIR_LUA_ECS_DEFAULT_MODE=EVAL` at cmake configure time) keeps the same source but registers everything at runtime via the LuaJIT-backed sol2 dispatch path — useful for dev iteration without a rebuild.

Reference demo: `creations/demos/lua_perf_grid/` (CODEGEN/EVAL toggle) and `creations/demos/lua_pipeline_demo/` (pipeline composition + mixing prefab systems with a Lua system).

### Lua-side APIs

#### `IRComponent.register` — declare a new component

```lua
-- Short form: type inferred from default value
IRComponent.register("Hp", {
    current = 100,       -- int32  (Lua integer)
    max     = 100,       -- int32
    regen   = 1.5,       -- float  (Lua float, non-integer literal)
    alive   = true,      -- bool
    tag     = "hero",    -- std::string
})

-- Explicit form: force a type or use whole-number float defaults
IRComponent.register("Vel", {
    x = { type = "float",  default = 0 },
    y = { type = "float",  default = 0 },
    z = { type = "float",  default = 0 },
})

-- Supported explicit types: "int32", "float", "bool", "string", "table"
-- "table" opts out of native storage (sol::table per entity, slower).

-- Referencing an existing C++ prefab component by name:
IRSystem.registerSystem({
    name = "TickCounterLua",
    components = { IRComponent.C_Position3D },   -- C++ prefab component
    tick = function(arch) ... end,
})
```

**Type inference rules:**

| Lua default                    | C++ column type      |
|--------------------------------|----------------------|
| integer literal (`5`, `42`)    | `int32_t`            |
| float literal (`1.5`, `0.016`) | `float`              |
| boolean (`true` / `false`)     | `bool`               |
| string (`"hello"`)             | `std::string`        |
| whole-number float (`0.0`)     | ambiguous — use explicit form |

Whole-number float defaults (e.g. `0.0`) are indistinguishable from `0` at the C level under LuaJIT — use `{ type = "float", default = 0 }` for those. A bad inference is a hard error at script-load time naming the offending field.

**Field order:** the codegen-emitted C++ struct and `ComponentName.new(...)` constructor list fields in **alphabetical order** (T-106 invariant). Match that order when constructing values in `setAt` calls.

#### `IRSystem.registerSystem` — declare a new system

```lua
local sysId = IRSystem.registerSystem({
    name       = "Move",                          -- unique system name
    components = { "Pos", "Vel" },               -- Lua component names OR IRComponent.C_X
    excludes   = { "Dead" },                     -- optional
    mode       = "eval",                          -- optional: "eval" overrides default mode
    tick       = function(arch)                   -- per-archetype tick body
        for i = 0, arch.length - 1 do
            local pos = arch.Pos:at(i)
            local vel = arch.Vel:at(i)
            arch.Pos:setAt(i, Pos.new(pos.x + vel.x, pos.y + vel.y))
        end
    end,
})
-- sysId is a SystemId Lua integer; park in a global if needed by C++.
```

**Tick body column API** (`arch.<ComponentName>`)

| Call | Purpose |
|------|---------|
| `arch.length` | entity count in this archetype |
| `arch.MyComp:at(i)` | read row `i` as a value (0-based) |
| `arch.MyComp:setAt(i, MyComp.new(...))` | write row `i` (constructor args in alphabetical field order) |
| `arch.MyComp:getField(i, "fieldName")` | read one field by name |
| `arch.MyComp:setField(i, "fieldName", value)` | write one field by name |

`getField` / `setField` field name: in CODEGEN mode, must be a **string literal** — the codegen tool requires a compile-time known field name. In EVAL mode, a string variable is accepted.

**Mode field:**
- Absent (or `mode = "codegen"`) — follows the creation's default mode (`CODEGEN` unless overridden).
- `mode = "eval"` — always registers at runtime via the EVAL path; the codegen tool skips C++ emission for this system. Useful for one-off systems during dev or when the tick body uses constructs the codegen DSL does not support (e.g. `while` loops, closures).

**Math:** `math.sin`, `math.cos`, `math.sqrt`, `math.abs`, `math.min`, `math.max`, `math.floor` map to `IRMath::` equivalents in codegen output (enforces the engine's no-`std::sin`-outside-`engine/math/` rule). Other `math.*` calls are a codegen-time error.

**Codegen DSL restrictions** (not applicable in EVAL mode):
- No `while` loops — use a single `if` branch or unroll.
- No captured upvalues inside the tick closure.
- No allocation (`table.new`, `{}` constructors) inside the tick body.
- No `string.format` or `io.*`.

#### `IRSystem.registerPipeline` — compose a pipeline

```lua
local SystemName = IRSystem.SystemName

IRSystem.registerPipeline(IRTime.UPDATE, {
    IRSystem.systemId(SystemName.GLOBAL_POSITION_3D),  -- prefab system
    IRSystem.systemId(SystemName.LIFETIME),
    tickCounterSysId,                                   -- Lua-defined system id
})

IRSystem.registerPipeline(IRTime.RENDER, {
    IRSystem.systemId(SystemName.FRAMEBUFFER_TO_SCREEN),
})
```

`IRSystem.systemId(SystemName.X)` looks up a prefab system by enum name. `IRSystem.registerSystem` returns a `SystemId` you can pass directly into the pipeline list — mix prefab and Lua-defined systems freely.

---

### C++ side: wiring the codegen path

In `main_lua.cpp`, add these calls **inside the `IREngine::registerLuaBindings` lambda**, before `script.scriptFile(...)`:

```cpp
#include "my_creation_codegen.hpp"   // generated by irreden_lua_codegen()

// 1. Enable the Lua-driven ECS surface (IRComponent.register, IRSystem.registerSystem, etc.)
script.bindLuaDrivenEcs();

// 2. Pre-register any C++ prefab components the Lua systems reference.
script.registerTypeFromTraits<IRComponents::C_Position3D>();

// 3. Pre-bind all codegen-emitted components so IRComponent.register is
//    idempotent at runtime and the codegen structs are live before scriptFile().
IRScript::CodegenRegistry::registerCodegenComponents(script);

// 4. Mirror the build-time default mode into the runtime so unmarked
//    IRSystem.registerSystem calls follow the same dispatch path.
script.setEcsDefaultMode(IRScript::CodegenRegistry::kDefaultEcsMode);

// 5. Load the Lua source. In CODEGEN builds, registerSystem no-ops for
//    unmarked calls; in EVAL builds it registers via the dynamic path.
script.scriptFile(IREngine::resolveScriptPath("main.lua").c_str());
```

**Resolving the system ID in CODEGEN vs EVAL builds:**

```cpp
// Helper to avoid instantiating CodegenSystemIds fields in EVAL builds.
template <class Ids> IRSystem::SystemId waveTickFromIds(const Ids &ids) {
    return ids.LuaWaveTick;
}

IRSystem::SystemId resolveLuaWaveTickId(IRScript::LuaScript &script) {
    using IRScript::EcsMode;
    if constexpr (IRScript::CodegenRegistry::kDefaultEcsMode == EcsMode::CODEGEN) {
        const auto ids = IRScript::CodegenRegistry::registerCodegenSystems();
        return waveTickFromIds(ids);
    } else {
        // EVAL build: main.lua parked the id as a Lua global.
        const sol::object obj = script.lua()["LuaWaveTickSysId"];
        return static_cast<IRSystem::SystemId>(obj.as<lua_Integer>());
    }
}
```

---

### CMake: `irreden_lua_codegen()`

```cmake
irreden_lua_codegen(<target>
    SOURCES <input1.lua> [input2.lua ...]
    OUTPUT_HPP <path/to/generated.hpp>
    [DEFAULT_MODE <CODEGEN|EVAL>]   # default CODEGEN
)
```

Adds a custom command that re-runs the `ir_lua_codegen` tool whenever any
`SOURCES` change, regenerates `OUTPUT_HPP`, and wires the output into the
target's sources and include path automatically.

**Example** (from `creations/demos/lua_perf_grid/CMakeLists.txt`):

```cmake
irreden_lua_codegen(IRLuaPerfGrid
    SOURCES main.lua
    OUTPUT_HPP ${CMAKE_CURRENT_BINARY_DIR}/codegen/lua_perf_grid_codegen.hpp
)
```

**Flipping to EVAL mode** without editing `CMakeLists.txt`:

```bash
cmake --preset linux-debug -DIR_LUA_ECS_DEFAULT_MODE=EVAL
```

The `IR_LUA_ECS_DEFAULT_MODE` cache variable overrides the `DEFAULT_MODE`
parameter when `DEFAULT_MODE` is omitted. `CODEGEN` is the default when
neither is set.

---

## Step 4: config.lua

Parsed at startup for window and runtime configuration:

```lua
config = {
    window = {
        width = 1280,
        height = 720,
        title = "My Creation"
    },
    video = {
        fit_mode = "pixel_perfect"
    }
}
```

## Step 5: main.lua

Main Lua entry point. Use `dofile` to load sub-scripts:

```lua
SCRIPT_DIR = "scripts/"

dofile(SCRIPT_DIR .. "settings.lua")
dofile(SCRIPT_DIR .. "entities.lua")

-- Entity creation, scene setup, etc.
```

`SCRIPT_DIR` resolves to `ExeDir/ExeStem/scripts/` at runtime.

## Step 6: CMake Script Sync

Add custom commands to copy Lua files into the build directory. See the `create-creation` skill for the full CMake pattern. Key points:
- `main.lua` and `config.lua` sync to `${RUNTIME_DIR}/scripts/`
- `scripts/` subdirectory copies via `copy_directory`
- The `*Run` target re-copies on each launch so script edits take effect without rebuilding C++

---

## Reference Creations

- **Full classic binding example:** `creations/demos/default/lua_bindings.cpp` (types, enums, components, API tables)
- **Codegen/EVAL toggle:** `creations/demos/lua_perf_grid/` — Lua-defined `LuaWaveState` component + `LuaWaveTick` system with `irreden_lua_codegen()` wiring. See `main.lua` for the schema/tick body and `main_lua.cpp` for the C++ side.
- **Pipeline composition:** `creations/demos/lua_pipeline_demo/` — entire `initSystems` in `main.lua`, mixes prefab systems with a Lua-defined system via `IRSystem.registerPipeline`.
- **Codegen test fixtures:** `test/script/lua_component_codegen_fixtures.lua` (component schema shapes), `test/script/lua_system_codegen_fixtures.lua` (system DSL patterns including `excludes`, `getField`/`setField`, math intrinsics).
- **Coexistence (CODEGEN + EVAL in one file):** `test/script/lua_system_coexistence_fixtures.lua` — one unmarked system (CODEGEN) and one `mode = "eval"` system in the same `.lua` source.
- **Component pack:** `creations/demos/default/lua_component_pack.hpp`
- **Design doc:** `docs/design/lua-driven-ecs.md` — full API contract, locked design choices, and the perf-parity gate spec.

---

## Exposed Lua Namespaces

| Namespace | Typical Contents |
|-----------|-----------------|
| `IREntity` | `createEntity`, `destroyEntity`, batch factories |
| `IRComponent` | `register`, `bindField`, `C_<PrefabName>` handles |
| `IRSystem` | `registerSystem`, `registerPipeline`, `systemId`, `replaceSystemBody`, `SystemName` |
| `IRTime` | `UPDATE`, `INPUT`, `RENDER` pipeline event keys |
| `IRAudio` | `openMidiOut`, `openMidiIn`, `rootNote`, scale helpers |
| `IRRender` | `setGuiScale`, `getMainCanvasSize`, `measureText` |
| `IRText` | `create`, `setText`, `remove` |
| `IRModifier` | `add`, `Transform` enum (ADD / MULTIPLY / SET / CLAMP_MIN / CLAMP_MAX / OVERRIDE) |
