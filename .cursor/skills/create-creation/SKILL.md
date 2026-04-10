---
name: create-creation
description: >-
  Scaffold a new Irreden Engine creation (demo, editor, or game) with all
  required files: CMakeLists.txt, C++ entry point, optional Lua wiring, and
  pipeline registration. Use when the user wants to create a new project,
  demo, editor, or game within the engine.
---

# Create a New Creation

## Overview

A "creation" is an executable that links against the engine. Creations live under `creations/` grouped by category: `demos/`, `editors/`, `hana_class_projects/`, or the special `game/` path which auto-registers.

## Directory Layout

```
creations/<category>/<name>/
├── CMakeLists.txt
├── main.cpp            # C++-only entry point
├── main_lua.cpp        # Lua-capable entry point (pick one)
├── lua_bindings.hpp    # If using Lua
├── lua_bindings.cpp    # If using Lua
├── lua_component_pack.hpp  # If using Lua
├── config.lua          # If using Lua
├── main.lua            # If using Lua
└── scripts/            # Sub-scripts loaded by main.lua
```

For a C++-only creation, only `CMakeLists.txt` and `main.cpp` are needed.

## Step 1: CMakeLists.txt

Use this template, replacing `YourCreation` / `YOUR_CREATION` with the actual name:

```cmake
set(IR_YOUR_CREATION_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_YOUR_CREATION_RUNTIME_DIR})
add_executable(IRYourCreation main.cpp)
target_link_libraries(IRYourCreation PUBLIC IrredenEngine)

add_custom_target(IRYourCreationAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_YOUR_CREATION_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_YOUR_CREATION_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_YOUR_CREATION_RUNTIME_DIR}/shaders
)

add_dependencies(IRYourCreation IRYourCreationAssets)

add_custom_target(IRYourCreationRun
    COMMAND $<TARGET_FILE:IRYourCreation>
    DEPENDS IRYourCreation
    WORKING_DIRECTORY ${IR_YOUR_CREATION_RUNTIME_DIR}
    USES_TERMINAL
)
```

For Lua creations, also add script-sync commands. See `creations/demos/default/CMakeLists.txt` or `creations/demos/midi_polyrhythm/CMakeLists.txt` for the full pattern with `copy_if_different` for `.lua` files and `scripts/` directories.

## Step 2: Register the creation

**Option A (engine-owned):** Add to `creations/CMakeLists.txt`:

```cmake
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/<category>/<name>)
```

**Option B (private game):** Place in `creations/game/`. The root `CMakeLists.txt` auto-adds it when `creations/game/CMakeLists.txt` exists.

## Step 3: C++ Entry Point

### Pure C++ (`main.cpp`)

```cpp
#include <irreden/ir_engine.hpp>

// COMPONENTS
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

// SYSTEMS
#include <irreden/update/systems/system_velocity.hpp>
#include <irreden/update/systems/system_update_positions_global.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/update/systems/system_lifetime.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>

// COMMANDS
#include <irreden/input/commands/command_close_window.hpp>
#include <irreden/render/commands/command_zoom_in.hpp>
#include <irreden/render/commands/command_zoom_out.hpp>

void initSystems();
void initCommands();

int main(int argc, char **argv) {
    IREngine::init("config.json");
    initSystems();
    initCommands();
    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::VELOCITY_3D>(),
         IRSystem::createSystem<IRSystem::GLOBAL_POSITION_3D>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
         IRSystem::createSystem<IRSystem::LIFETIME>()}
    );
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );
    IRSystem::registerPipeline(
        IRTime::Events::RENDER,
        {IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
         IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_2>(),
         IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
         IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>()}
    );
}

void initCommands() {
    IRCommand::createCommand<IRCommand::CLOSE_WINDOW>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonEscape
    );
    IRCommand::createCommand<IRCommand::ZOOM_IN>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonEqual
    );
    IRCommand::createCommand<IRCommand::ZOOM_OUT>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonMinus
    );
}
```

### Lua-capable (`main_lua.cpp`)

The key difference: call `registerLuaBindings()` **before** `IREngine::init(argv[0])`:

```cpp
#include <irreden/ir_engine.hpp>
#include "lua_bindings.hpp"

// ... same includes as above, plus Lua-bound components/systems ...

int main(int argc, char **argv) {
    MyCreation::registerLuaBindings();
    IREngine::init(argv[0]);
    initSystems();
    initCommands();
    IREngine::gameLoop();
    return 0;
}
```

For the Lua binding files, see the `lua-creation-setup` skill.

## Pipeline Ordering

Pipelines execute in this order each frame: **INPUT -> UPDATE -> RENDER**.

Within each pipeline, systems execute in the order listed in `registerPipeline`. Common orderings:

**UPDATE:** velocity/acceleration -> goto/easing -> global position -> voxel children -> lifetime

**INPUT:** key/mouse -> gamepad -> hover detect

**RENDER:** camera pan -> velocity render -> voxel-to-trixel stages -> shapes/text -> trixel compositing -> framebuffer -> debug overlay -> screen

## Reference Creations

- **Minimal C++:** `creations/demos/default/main.cpp`
- **Full Lua:** `creations/demos/midi_polyrhythm/` (complete Lua stack with MIDI)
- **C++ + Lua hybrid:** `creations/demos/default/main_lua.cpp`
