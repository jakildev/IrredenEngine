---
name: create-creation
description: >-
  Scaffolds a new Irreden Engine creation (demo, editor, or game): the
  CMakeLists.txt with engine linkage and asset staging, the C++ entry point
  with pipeline and command registration, optional Lua wiring, and the
  registration in `creations/CMakeLists.txt`. Use when the user wants to
  create a new project, demo, editor, or game within the engine.
---

# Create a New Creation

A creation is an executable linking the engine, under `creations/<category>/`
(`demos/`, `editors/`, `hana_class_projects/`, or the private `game/` path).
Minimum file sets, visibility tiers, and the asset-staging rule:
[`creations/CLAUDE.md`](../../../creations/CLAUDE.md); demo conventions:
[`creations/demos/CLAUDE.md`](../../../creations/demos/CLAUDE.md) §"Adding a
new demo". References: `creations/demos/shape_debug/CMakeLists.txt` (canonical
CMake), `creations/demos/default/main.cpp` (C++-only),
`creations/demos/default/main_lua.cpp` plus its binding files (Lua).

## 1. CMakeLists.txt

Replace `YourCreation` / `YOUR_CREATION` with the real name — they are scaffold
sentinels that `simplify` flags if they survive into source:

```cmake
set(IR_YOUR_CREATION_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_YOUR_CREATION_RUNTIME_DIR})
add_executable(IRYourCreation main.cpp)
target_link_libraries(IRYourCreation PUBLIC IrredenEngine)

# Exe-relative data/ shaders/ scripts/ (+ Windows DLLs) and the
# IRYourCreationPackage bundle target. Never hand-roll an *Assets target.
irreden_bundle_assets(IRYourCreation SCRIPTS config.lua)
irreden_package_target(IRYourCreation)

add_custom_target(IRYourCreationRun
    COMMAND $<TARGET_FILE:IRYourCreation>
    DEPENDS IRYourCreation IRYourCreationAssets
    WORKING_DIRECTORY ${IR_YOUR_CREATION_RUNTIME_DIR}
    USES_TERMINAL
)
```

A Lua creation lists every script in `SCRIPTS` (`main.lua config.lua ...`, as
in `creations/demos/default/CMakeLists.txt`); Lua-defined components or
systems add `irreden_lua_codegen(...)` (`lua-creation-setup` skill).

## 2. Register

- Engine-owned: append
  `add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/<category>/<name>)` to
  `creations/CMakeLists.txt` — append, never reorder.
- Private game: place it in `creations/game/`; the root `CMakeLists.txt`
  auto-adds it when `creations/game/CMakeLists.txt` exists.

## 3. Entry point

### `main.cpp`

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
    IREngine::init(argc, argv);
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

### `main_lua.cpp`

`registerLuaBindings()` runs **before** `IREngine::init(argc, argv)`; the rest
is identical:

```cpp
#include <irreden/ir_engine.hpp>
#include "lua_bindings.hpp"
// ... same includes as above, plus Lua-bound components/systems ...

int main(int argc, char **argv) {
    MyCreation::registerLuaBindings();
    IREngine::init(argc, argv);
    initSystems();
    initCommands();
    IREngine::gameLoop();
    return 0;
}
```

Binding files: `lua-creation-setup` skill.

## Pipeline order

INPUT → UPDATE → RENDER and the orderings within each pipeline:
[`engine/system/CLAUDE.md`](../../../engine/system/CLAUDE.md) §Pipelines. The
RENDER minimum is `VOXEL_TO_TRIXEL_STAGE_1`, `TRIXEL_TO_FRAMEBUFFER`,
`FRAMEBUFFER_TO_SCREEN` (`render-trixel-pipeline` skill).

## Done when

`fleet-build --target IRYourCreation` is green and
`fleet-run --timeout 15 IRYourCreation` runs from the exe directory
([`docs/agents/BUILD.md`](../../../docs/agents/BUILD.md) §"Running an
executable").
