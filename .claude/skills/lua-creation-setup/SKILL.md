---
name: lua-creation-setup
description: >-
  Set up Lua scripting for an Irreden Engine creation: bindings, component
  packs, config files, and script wiring. Use when the user wants to add Lua
  support to a creation, register Lua bindings, expose components to Lua, or
  work with config.lua/main.lua scripts.
---

# Lua Creation Setup

## Overview

Lua 5.4 + sol2. C++ sets up systems and pipelines; Lua drives entity creation, configuration, and runtime game logic. Lua bindings must be registered **before** `IREngine::init`.

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

## Step 1: lua_bindings.hpp

Declare the registration function in your creation's namespace:

```cpp
#ifndef CREATIONS_MYCREATION_LUA_BINDINGS_H
#define CREATIONS_MYCREATION_LUA_BINDINGS_H

namespace MyCreation {
void registerLuaBindings();
}

#endif
```

## Step 2: lua_bindings.cpp

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

### Key registration methods

| Method | Purpose |
|--------|---------|
| `registerType<T, Constructors...>(name, key, &T::member, ...)` | Register a plain type with named fields |
| `registerEnum<E>(name, {{key, value}, ...})` | Register an enum as a Lua table |
| `registerTypesFromTraits<C_A, C_B, ...>()` | Bulk-register components that have `*_lua.hpp` traits |
| `registerCreateEntityFunction<Components...>(name)` | Register an `IREntity.name(...)` Lua factory |
| `registerCreateEntityBatchFunction<Components...>(name)` | Register a batch factory for multiple entities |

## Step 3: lua_component_pack.hpp

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

## Opting a Component into Lua

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

## Reference Creations

- **Full example:** `creations/demos/default/lua_bindings.cpp` (types, enums, components, API tables)
- **MIDI + Lua:** `creations/demos/midi_polyrhythm/lua_bindings.cpp` (music theory, batch factories)
- **Component pack:** `creations/demos/default/lua_component_pack.hpp`

## Exposed Lua Namespaces

| Namespace | Typical Contents |
|-----------|-----------------|
| `IREntity` | `createEntity`, `destroyEntity`, batch factories |
| `IRAudio` | `openMidiOut`, `openMidiIn`, `rootNote`, scale helpers |
| `IRRender` | `setGuiScale`, `getMainCanvasSize`, `measureText` |
| `IRText` | `create`, `setText`, `remove` |
