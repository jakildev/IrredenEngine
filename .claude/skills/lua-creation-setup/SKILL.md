---
name: lua-creation-setup
description: >-
  Sets up Lua scripting for an Irreden Engine creation — bindings, component
  packs, config and entry scripts, and Lua-defined components/systems via the
  codegen (build-time C++) or EVAL (runtime LuaJIT) path. Use when the user
  wants to add Lua support to a creation, register Lua bindings, expose
  components to Lua, define new components/systems in Lua, or compose
  pipelines from Lua.
---

# Lua Creation Setup

LuaJIT 2.1 + sol2. C++ registers systems and pipelines; Lua drives entity
creation, configuration, and game logic. Bindings register **before**
`IREngine::init`. The API reference for everything below is
[`engine/script/CLAUDE.md`](../../../engine/script/CLAUDE.md); this skill is
the procedure.

Two paths, often combined, sharing one `ComponentId` space and archetype graph:

1. **Expose C++ types to Lua** — `lua_bindings.cpp` + `lua_component_pack.hpp`
   (Part A).
2. **Define components and systems in Lua** — `IRComponent.register` /
   `IRSystem.registerSystem`, emitted as C++ at build time (CODEGEN) or
   registered at runtime (EVAL) (Part B).

## Files

`creations/<category>/<name>/`: `main_lua.cpp` (registerLuaBindings → init →
loop), `lua_bindings.{hpp,cpp}`, `lua_component_pack.hpp`, `config.lua`,
`main.lua` (dofiles `scripts/`). A codegen creation also runs
`irreden_lua_codegen()` on its `.lua` schema from `CMakeLists.txt`. Minimum
file set and asset staging: [`creations/CLAUDE.md`](../../../creations/CLAUDE.md).

## Part A — exposing C++ types

### lua_bindings.hpp

```cpp
#ifndef CREATIONS_MYCREATION_LUA_BINDINGS_H
#define CREATIONS_MYCREATION_LUA_BINDINGS_H

namespace MyCreation {
void registerLuaBindings();
}

#endif
```

### lua_bindings.cpp

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

        luaScript.registerType<Color, Color(int, int, int, int)>(
            "Color", "r", &Color::red_, "g", &Color::green_,
            "b", &Color::blue_, "a", &Color::alpha_
        );
        luaScript.registerType<ivec3, ivec3(int, int, int)>(
            "ivec3", "x", &ivec3::x, "y", &ivec3::y, "z", &ivec3::z
        );

        luaScript.registerEnum<IREasingFunctions>(
            "IREasingFunction",
            {{"LINEAR_INTERPOLATION", kLinearInterpolation},
             {"QUADRATIC_EASE_IN", kQuadraticEaseIn}}
        );

        registerLuaComponentPack(luaScript);

        luaScript.registerCreateEntityFunction<C_MidiSequence>("createMidiSequence");
        luaScript.registerCreateEntityBatchFunction<
            C_Position3D, C_VoxelSetNew, C_PeriodicIdle>(
            "createEntityBatchVoxelPeriodicIdle"
        );

        luaScript.lua()["IRAudio"] = luaScript.lua().create_table();
        luaScript.lua()["IRAudio"]["openMidiOut"] = [](const std::string &name) {
            return IRAudio::openPortMidiOut(name);
        };
    });

    isRegistered = true;
}
} // namespace MyCreation
```

`LuaScript` registration surface: `registerType<T, Ctors...>(name, key,
&T::member, ...)`, `registerEnum<E>(name, {{key, value}, ...})`,
`registerTypeFromTraits<C>()` / `registerTypesFromTraits<Cs...>()` for
components with `*_lua.hpp` traits, and `registerCreateEntityFunction<Cs...>(name)`
/ `registerCreateEntityBatchFunction<Cs...>(name)` for `IREntity.<name>(...)`
factories.

### lua_component_pack.hpp

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

### Opting a C++ component into Lua

A `*_lua.hpp` beside the component specialises `kHasLuaBinding<T> = true` and
implements `bindLuaType<T>(LuaScript&)`
([`engine/prefabs/CLAUDE.md`](../../../engine/prefabs/CLAUDE.md) §Conventions):

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

Then include it in `lua_component_pack.hpp` and add the type to
`registerTypesFromTraits<...>()`.

## Part B — Lua-defined components and systems

`cmake/lua_codegen` reads the `.lua` source at build time and emits a typed C++
struct per component and a `createSystem` specialisation per system (CODEGEN).
The same file loads at runtime: `IRComponent.register` resolves to the
pre-bound handle and `IRSystem.registerSystem` no-ops or registers per mode.
`-DIR_LUA_ECS_DEFAULT_MODE=EVAL` at configure time registers everything at
runtime instead — script edits without a rebuild. Reference demos:
`creations/demos/lua_perf_grid/` (CODEGEN/EVAL toggle) and
`creations/demos/lua_pipeline_demo/` (pipeline composition).

### Lua side

```lua
-- Component: type inferred from the default, or explicit.
IRComponent.register("Hp", { current = 100, regen = 1.5, alive = true, tag = "hero" })
IRComponent.register("Vel", {
    x = { type = "float", default = 0 },
    y = { type = "float", default = 0 },
})

-- System: Lua component names or IRComponent.C_X prefab handles.
local moveId = IRSystem.registerSystem({
    name       = "Move",
    components = { "Pos", "Vel" },
    excludes   = { "Dead" },                 -- optional
    mode       = "eval",                     -- optional; forces the runtime path
    tick       = function(arch)
        for i = 0, arch.length - 1 do
            local pos, vel = arch.Pos:at(i), arch.Vel:at(i)
            arch.Pos:setAt(i, Pos.new(pos.x + vel.x, pos.y + vel.y))
        end
    end,
})

-- Pipeline: prefab and Lua systems mix freely.
IRSystem.registerPipeline(IRTime.UPDATE, {
    IRSystem.systemId(IRSystem.SystemName.GLOBAL_POSITION_3D),
    moveId,
})
```

Contracts (full text in `engine/script/CLAUDE.md`, section named per bullet):

- **Type inference** — integer literal → `int32`, float literal → `float`,
  boolean → `bool`, string → `std::string`; explicit `type` is one of
  `int32|float|bool|string|table` (`table` opts out of native storage). A
  whole-number float default (`0.0`) is indistinguishable from `0` — use the
  explicit form. A bad inference is a load-time error naming the field.
  §"Lua-defined components (`IRComponent.register`)".
- **Field order** — the emitted struct and `Name.new(...)` list fields
  alphabetically; `setAt` constructor arguments follow that order.
- **Column API** — `arch.length`, `arch.C:at(i)` (0-based), `arch.C:setAt(i,
  C.new(...))`, `arch.C:getField(i, "f")`, `arch.C:setField(i, "f", v)`; in
  CODEGEN mode the field name must be a string literal.
- **`mode`** — absent or `"codegen"` follows the creation default; `"eval"`
  always registers at runtime and the codegen tool skips it — use it for
  constructs the DSL rejects. §"Per-system mode override + CODEGEN/EVAL
  coexistence".
- **Codegen DSL** (CODEGEN only) — no `while`, no captured upvalues, no
  allocation (`{}`, `table.new`) in the tick, no `string.format` / `io.*`;
  `math.sin/cos/sqrt/abs/min/max/floor` map to `IRMath::`, any other `math.*`
  is a codegen error. §"CODEGEN system bodies".

### C++ side

In `main_lua.cpp`, inside the `IREngine::registerLuaBindings` lambda, before
`scriptFile`:

```cpp
#include "my_creation_codegen.hpp"   // emitted by irreden_lua_codegen()

script.bindLuaDrivenEcs();                                     // IRComponent.register, IRSystem.registerSystem, ...
script.registerTypeFromTraits<IRComponents::C_Position3D>();   // every prefab component the Lua systems reference
IRScript::CodegenRegistry::registerCodegenComponents(script);  // pre-bind codegen structs; makes register idempotent
script.setEcsDefaultMode(IRScript::CodegenRegistry::kDefaultEcsMode);
script.scriptFile(IREngine::resolveScriptPath("main.lua").c_str());
```

Resolving a Lua system's `SystemId`: a CODEGEN build reads
`IRScript::CodegenRegistry::registerCodegenSystems().<Name>`; an EVAL build
reads the id `main.lua` parked in a global
(`script.lua()["<Name>SysId"].as<lua_Integer>()`). Branch with
`if constexpr (CodegenRegistry::kDefaultEcsMode == EcsMode::CODEGEN)` and keep
the `CodegenSystemIds` field access behind a template helper so EVAL builds
never instantiate it.

### CMake

```cmake
irreden_lua_codegen(<target>
    SOURCES <input1.lua> [input2.lua ...]
    OUTPUT_HPP <path/to/generated.hpp>
    [DEFAULT_MODE <CODEGEN|EVAL>]   # default CODEGEN
)
```

Re-runs `ir_lua_codegen` whenever a `SOURCES` file changes and adds the output
to the target's sources and include path. With `DEFAULT_MODE` omitted, the
cache variable wins: `cmake --preset linux-debug -DIR_LUA_ECS_DEFAULT_MODE=EVAL`
(`engine/script/CLAUDE.md` §"Build-time codegen of Lua-defined components
(CODEGEN mode)").

## config.lua and main.lua

```lua
-- config.lua: parsed at startup
config = {
    window = { width = 1280, height = 720, title = "My Creation" },
    video  = { fit_mode = "pixel_perfect" },
}
```

```lua
-- main.lua
SCRIPT_DIR = "scripts/"
dofile(SCRIPT_DIR .. "settings.lua")
dofile(SCRIPT_DIR .. "entities.lua")
```

`SCRIPT_DIR` resolves to `ExeDir/ExeStem/scripts/` at runtime
(`engine/script/CLAUDE.md` §"Script resolution"). Stage every `.lua` with
`irreden_bundle_assets(<target> SCRIPTS main.lua config.lua ...)`; the
`<target>Run` target re-copies on launch so script edits need no C++ rebuild
(`create-creation` skill).

## References

- `creations/demos/default/lua_bindings.cpp` and `lua_component_pack.hpp` —
  classic bindings (types, enums, components, API tables).
- `test/script/lua_component_codegen_fixtures.lua`,
  `lua_system_codegen_fixtures.lua`, `lua_system_coexistence_fixtures.lua` —
  schema shapes, DSL patterns, CODEGEN + EVAL in one file.
- `docs/design/lua-driven-ecs.md` — API contract and the perf-parity gate.
- Lua namespaces (`IREntity`, `IRComponent`, `IRSystem`, `IRTime`, `IRAudio`,
  `IRRender`, `IRText`, `IRModifier`): per-namespace sections of
  `engine/script/CLAUDE.md`.
