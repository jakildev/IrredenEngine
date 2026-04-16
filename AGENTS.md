# Irreden Engine – AI Agent Guide

Irreden is an isometric "pixelatable" voxel content and game engine built around an archetype-based ECS. Game logic lives in components and systems; Lua is used for configuration and high-level creation logic. C++ handles systems, pipelines, and engine bindings.

---

## Project Structure

```
IrredenEngine/
├── cmake/                  # CMake utility scripts (ir_functions, ir_quality_tools, etc.)
├── creations/              # Applications and demos
│   ├── demos/              # default, midi_polyrhythm, midi_keyboard, shape_debug, metal_clear_test
│   ├── editors/            # font_maker
│   └── game/               # Conventional private game path (gitignored)
├── docs/                   # Style, dependencies, contributing
├── engine/                 # Core engine (static libs)
│   ├── asset/              # IrredenEngineAsset
│   ├── audio/              # IrredenEngineAudio (MIDI, RtMidi, RtAudio)
│   ├── command/            # IrredenEngineCommand (input-triggered actions)
│   ├── common/             # IrredenEngineCommon (shared types, Color, constants)
│   ├── data/               # Default configs and images (copied to build at POST_BUILD)
│   ├── entity/             # IrredenEngineEntity (IRECS – archetype ECS)
│   ├── include/irreden/    # Top-level entry point: ir_engine.hpp only
│   ├── input/              # IrredenEngineInput (GLFW, keyboard, mouse, gamepad)
│   ├── math/               # IrredenEngineMath (GLM, easing, color, nav grid, pathfinding)
│   ├── prefabs/            # Header-only components, systems, commands, entities
│   ├── profile/            # IrredenEngineProfile (easy_profiler)
│   ├── render/             # IrredenEngineRendering (OpenGL, trixel pipeline, shaders, text)
│   ├── script/             # IrredenEngineScripting (Lua 5.4, sol2)
│   ├── system/             # IrredenEngineSystem (pipeline scheduling)
│   ├── time/               # IrredenEngineTime (fixed-step timing)
│   ├── video/              # IrredenEngineVideo (video capture, screenshots)
│   ├── window/             # IrredenEngineWindow (GLFW window)
│   └── world/              # World class (game loop, owns all managers)
└── test/
```

---

## Engine Entry Point and Module Includes

**`engine/include/irreden/ir_engine.hpp`** is the single top-level entry point. It creates the `World`, sets `cwd` to the exe directory, and derives the scripts directory from the exe stem name (e.g. `IRMidiPolyrhythm/`).

Each module also exposes its own `ir_*.hpp` header within its own include tree:

```
engine/system/include/irreden/ir_system.hpp
engine/entity/include/irreden/ir_entity.hpp
engine/math/include/irreden/ir_math.hpp
engine/audio/include/irreden/ir_audio.hpp
engine/script/include/irreden/ir_script.hpp
...
```

Creations include these directly. Do not include internal module headers — use the `ir_*.hpp` entry points.

---

## The `World` Class

`engine/world/include/irreden/world.hpp` defines `class World`, the true runtime root. It owns:

- `EntityManager`, `SystemManager`, `InputManager`, `CommandManager`
- `RenderManager`, `RenderingResourceManager`
- `AudioManager`, `TimeManager`, `VideoManager`
- `IRGLFWWindow`, `LuaScript`

`IREngine::init()` constructs the `unique_ptr<World>`. All subsystem access flows through `World`.

---

## ECS Philosophy: Logic in Components and Systems

**Game logic belongs in the ECS, not scattered in scripts or one-off code.**

1. **Components** – Plain data structs (`C_` prefix). No logic.
2. **Systems** – Lambda-based logic operating on entities with a matching component set.
3. **Entities** – Created with component bundles; `createEntity` implicitly adds `C_PositionGlobal3D` and `C_PositionOffset3D`.

`SystemId` is an alias for `EntityId` — systems are themselves entities stored in the ECS.

### Component Conventions

- **Namespace:** `IRComponents`
- **Naming:** `C_` prefix (e.g. `C_MoveOrder`, `C_Velocity3D`, `C_VoxelSetNew`).
- **Members:** Public members use trailing `_` (e.g. `targetCell_`, `velocity_`).
- **Location:** `engine/prefabs/irreden/<domain>/components/component_*.hpp`.

```cpp
namespace IRComponents {
struct C_MoveOrder {
    IRMath::ivec3 targetCell_;
    C_MoveOrder() : targetCell_{0, 0, 0} {}
    C_MoveOrder(IRMath::ivec3 targetCell) : targetCell_{targetCell} {}
};
}
```

### System Conventions

Prefab systems use `IRSystem::System<SYSTEM_NAME>` template specialization with a static `create()` returning `SystemId`. **Before adding a new prefab system, add its name to the `SystemName` enum in `ir_system_types.hpp`.**

- **Location:** `engine/prefabs/irreden/<domain>/systems/system_*.hpp`.

There are three valid tick-function signatures for `createSystem<>`:

**Per-component** (most common):
```cpp
template <> struct System<VELOCITY_DRAG> {
    static SystemId create() {
        return createSystem<C_Velocity3D, C_VelocityDrag>(
            "VelocityDrag",
            [](C_Velocity3D& velocity, C_VelocityDrag& drag) {
                // uses IRTime::deltaTime(IRTime::UPDATE)
            }
        );
    }
};
```

**Per-entity-id** (when you need to query other components by entity):
```cpp
createSystem<C_NavAgent, C_MoveOrder>(
    "GridPathfind",
    [](EntityId id, C_NavAgent& agent, C_MoveOrder& order) { ... }
);
```

**Per-archetype-node / batch** (bulk or SIMD-style processing):
```cpp
createSystem<C_NavAgent>(
    "GridBake",
    [](const Archetype& arch, std::vector<EntityId>& ids, std::vector<C_NavAgent>& agents) { ... }
);
```

### Avoid Per-Entity Component Queries Inside System Ticks

**Never call `getComponent` or `getComponentOptional` on individual entities inside a system's per-entity tick function.** Each call performs a hash-map lookup, a linear scan of the entity's archetype set, and another hash-map lookup into the component storage -- all with profiler overhead. At scale this dominates the frame.

**Alternatives (preferred → acceptable):**

1. **Include the component in the system's template parameters** so it's iterated directly from the archetype's dense column storage. This is the primary fix -- the data is accessed via contiguous array index, not random lookup.
2. **Store the data in an existing component at creation time** if it's known upfront (e.g. store a canvas entity ID in `C_VoxelSetNew` during allocation rather than looking it up every frame).
3. **Use `beginTick` / `endTick`** for lookups that only need to happen once per frame, not per entity.
4. **Use `relationTick`** for per-parent-group lookups -- fires once per unique parent entity when using `CHILD_OF` or other relation queries.
5. **Future:** Use relation-based archetype grouping (e.g. a `RENDERS_ON` relation) to partition entities by group at the archetype level, eliminating per-entity branching entirely.

### Entity Creation

- **Single:** `IREntity::createEntity(C_VoxelSetNew{...}, ...)`.
- **Batch:** `createEntityBatch(n, ...)` or `createEntityBatchWithFunctions(...)` for per-index init.
- Entities always receive position components automatically.

### Relations

ECS supports `CHILD_OF`, `PARENT_TO`, `SIBLING_OF` for hierarchy. Pass a `RelationParams<>` struct to `createSystem` to create relation-aware systems.

---

## Prefabs Layout

| Type        | Path pattern                            | Example                         |
|-------------|------------------------------------------|---------------------------------|
| Components  | `*/components/component_*.hpp`          | `component_move_order.hpp`      |
| Systems     | `*/systems/system_*.hpp`                | `system_velocity_drag.hpp`      |
| Commands    | `*/commands/command_*.hpp`              | `command_zoom_in.hpp`           |
| Entities    | `*/entities/entity_*.hpp`              | `entity_trixel_canvas.hpp`      |

Prefabs are grouped by domain under `engine/prefabs/irreden/`:

| Domain     | Contents                                                         |
|------------|------------------------------------------------------------------|
| `common/`  | Position, transform, name, player, tags                         |
| `update/`  | Physics, movement, collision, animation, particles, navigation  |
| `voxel/`   | Voxel rendering, voxel set management                           |
| `input/`   | Hover detection, keyboard/mouse/gamepad input                   |
| `render/`  | Trixel canvases, framebuffer, camera, text                      |
| `audio/`   | MIDI notes, sequences, messages, channels                       |
| `video/`   | Screenshot and recording commands                               |

**Note:** `engine/prefabs/irreden/update/nav_query.hpp` lives directly in the `update/` folder (not in `components/` or `systems/`). It contains free functions implementing the chunked A\* pathfinding query API.

---

## Commands

Commands are input-triggered actions defined via template specialization, similar to systems:

```cpp
IRCommand::createCommand<IRCommand::ZOOM_IN>(
    InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED, KeyMouseButtons::kKeyButtonEqual
);
```

Command definitions live in `engine/prefabs/irreden/<domain>/commands/command_*.hpp`. Input modifiers (Shift, Ctrl) are supported as additional arguments.

---

## Pipelines and Execution Order

- Pipelines are per-event: `IRTime::Events::UPDATE`, `INPUT`, `RENDER`.
- Creations register systems with `IRSystem::registerPipeline(event, {systemIds...})`.
- Order in the list defines execution order within each event.
- Systems run only on entities that have all required components.
- The loop runs: **input → update → render** (fixed-step update, variable render).

---

## Render Pipeline

**Voxels → Trixel Stage 1 → Trixel Stage 2 → Trixel-to-Framebuffer → Framebuffer-to-Screen**

- `TRIXEL_TO_TRIXEL` allows compositing multiple trixel canvases.
- `TEXT_TO_TRIXEL` is a separate stage for text rendering via `TrixelFont`.
- GLSL shaders live in `engine/render/src/shaders/` and are named with `c_`, `v_`, `f_`, `g_` prefixes.
- The view is orthographic isometric.

---

## Coordinate Systems and Math

All vector/matrix types are GLM aliases defined in the `IRMath` namespace (`ir_math_types.hpp`): `vec2`, `vec3`, `ivec3`, `mat4`, etc.

### 3D Voxel Space

- **X axis** — points to the lower-left in the isometric view.
- **Y axis** — points to the lower-right in the isometric view.
- **Z axis** — points upward (vertical axis).

The ground plane is XY. Entities at the same Z level sit on the same horizontal plane.

### Isometric Projection

3D positions map to 2D isometric (trixel canvas) coordinates via `IRMath::pos3DtoPos2DIso`:

```
iso.x = -x + y
iso.y = -x - y + 2z
```

Screen coordinates are derived by scaling and negating the iso coordinates (`IRMath::pos3DtoPos2DScreen`).

### Depth / Distance

Isometric depth is `distance = x + y + z` (`IRMath::pos3DtoDistance`). The depth axis is the (1,1,1) direction — objects with the same `x+y+z` project to the same depth layer. Higher distance = further from camera. `IRMath::isoDepthShift(pos, d)` shifts a position by `(d, d, d)`, changing depth without altering the 2D iso projection.

### Face Types

Each voxel exposes up to three visible faces in the isometric view:

| FaceType   | Perpendicular to | Visible side |
|------------|-------------------|--------------|
| `X_FACE`   | X axis            | Right face   |
| `Y_FACE`   | Y axis            | Left face    |
| `Z_FACE`   | Z axis            | Top face     |

Face assignment and per-face trixel placement are handled in the voxel-to-trixel compute shader (`c_voxel_to_trixel_stage_1.glsl`).

### Position Components

| Component             | Type    | Purpose                                            |
|-----------------------|---------|----------------------------------------------------|
| `C_PositionGlobal3D`  | `vec3`  | World-space 3D position (auto-added to entities)   |
| `C_PositionOffset3D`  | `vec3`  | Temporary additive offset (auto-added to entities) |
| `C_Position3D`        | `vec3`  | General-purpose 3D position                        |
| `C_PositionInt3D`     | `ivec3` | Integer 3D position (cell/grid coordinates)        |
| `C_Position2DIso`     | `vec2`  | 2D isometric grid position                         |
| `C_Position2D`        | `vec2`  | 2D screen / game-resolution position               |

`C_PositionGlobal3D` and `C_PositionOffset3D` are implicitly added by `createEntity`. The rendered position is `global + offset`.

### Isometric Planes and Axes

```cpp
enum class PlaneIso { XY = 0, XZ = 1, YZ = 2 };
enum class CoordinateAxis { XAxis = 0, YAxis = 1, ZAxis = 2 };
```

`PlaneIso` selects a 2D plane in isometric space: `XY` places entities in the horizontal ground plane (depth along Z), `XZ` uses depth along Y, `YZ` uses depth along X. Used by layout helpers (`layoutGridCentered`, `layoutCircle`, etc.).

### 2D Grid-to-Screen (Isometric Tiles)

For tile/object-based 2D isometric layouts, the basis vectors are:

```
iHat = ( 1.0,  0.5) * (objectSize / 2)
jHat = (-1.0,  0.5) * (objectSize / 2)
```

This is the standard 2:1 isometric diamond pattern (`kIHatGridToScreenIso` / `kJHatGridToScreenIso`).

### Navigation Grid Coordinates

The navigation system adds a chunked spatial layer on top of voxel space:

- **World cell** (`ivec3`) — integer grid coordinates in world space.
- **Chunk coordinate** (`ChunkCoord` = `ivec2`) — identifies which chunk a cell belongs to (XY only; Z is not chunked).
- **Local cell** (`ivec3`) — position within a chunk, relative to the chunk origin.
- Conversions: `worldCellToChunkCoord`, `worldCellToLocalCell`, `localCellToWorldCell` (in `nav_types.hpp`).

### Velocity

`C_Velocity3D` stores velocity in **blocks per second** as a `vec3`. Systems that integrate velocity use `IRTime::deltaTime(IRTime::UPDATE)` for fixed-step advancement.

---

## Lua Integration

Lua 5.4 + sol2. C++ sets up all systems and pipelines; Lua drives entity creation, configuration, and runtime game logic.

### File Layout for a Lua Creation

```
creations/demos/your_demo/
├── CMakeLists.txt
├── main_lua.cpp          # C++ entry: registerLuaBindings, IREngine::init, loop
├── lua_bindings.hpp      # Declares registerLuaBindings()
├── lua_bindings.cpp      # Registers enums, types, and components with sol2
├── lua_component_pack.hpp# Lists components to bulk-register; includes *_lua.hpp variants
├── config.lua            # Window, video, MIDI config (parsed at startup)
├── main.lua              # Main Lua entry; dofile() sub-scripts
├── scripts/              # Sub-scripts loaded by main.lua
└── lua_defs/
    └── irreden_api.lua   # Type definitions for Lua LSP (not loaded at runtime)
```

### Binding Pattern

1. **`config.lua`** – Defines a `config = { ... }` table. Parsed at startup for window size, resolution, fit mode, MIDI device, video capture, etc.

2. **`lua_component_pack.hpp`** – Lists which components to bulk-register. Include the `*_lua.hpp` variant of each component:
   ```cpp
   #include <irreden/audio/components/component_rhythmic_launch_lua.hpp>
   // ...
   using LuaComponentPack = std::tuple<C_RhythmicLaunch, ...>;
   ```

3. **`*_lua.hpp` component variants** – Opt a component into Lua by:
   - Specializing `kHasLuaBinding<T> = true`
   - Implementing `bindLuaType<T>(LuaScript&)` to register fields with sol2

4. **`lua_bindings.cpp`** – Calls `IREngine::registerLuaBindings(lambda)` before `IREngine::init`. Inside the lambda:
   - Register enums: `luaScript.registerEnum<>()`
   - Register plain types: `luaScript.registerType<T, Constructors...>(name, key, &T::member, ...)`
   - Bulk-register components: `luaScript.registerTypesFromTraits<C_A, C_B, ...>()`

5. **`main.lua`** – Uses `dofile(SCRIPT_DIR .. "name.lua")` to load sub-scripts. `SCRIPT_DIR` resolves to `ExeDir/ExeStem/scripts/`.

### Lua Namespaces Exposed to Scripts

`IREntity.*`, `IRAudio.*`, `IRInput.*`, `IRPhysics.*` — registered in `lua_bindings.cpp` via calls like `luaScript.registerCreateEntityFunction(...)`.

### Script Path Resolution

`IREngine::resolveScriptPath(filename)`: bare filenames resolve from `ExeDir/ExeStem/` (e.g. `IRMidiPolyrhythm/`); paths with a directory component resolve from cwd.

---

## Creations

1. Add a folder under `creations/demos/your_demo` (or `editors/`, etc.).
2. Add `CMakeLists.txt` with `add_executable(...)` and `target_link_libraries(... PUBLIC IrredenEngine)`.
3. Add the subdirectory to the parent `CMakeLists.txt`.
4. For C++-only creations: use `main.cpp`.
5. For Lua-driven creations: use `main_lua.cpp` + `lua_bindings.*` + `lua_component_pack.hpp` + Lua scripts. See `creations/demos/midi_polyrhythm/` as the reference.
6. Register pipelines in `IREngine::init`. Include only the components/systems your creation needs.

### Private Project Workflow

- Keep engine-owned creations in `creations/demos/`, `creations/editors/`, etc.
- For a private or user-specific game, prefer the conventional `creations/game/` path. The root `CMakeLists.txt` auto-adds it when `creations/game/CMakeLists.txt` exists, so the game shows up in the same root build graph as the engine demos.
- Treat `creations/game/` like a demo-style integration:
  - Put shared game code in a library target such as `IRGameLib`
  - Put the runnable creation in a target such as `IRGame`
  - Add helper targets such as `IRGameAssets` and `IRGameRun` when the game needs synced scripts or data
- Use root configure for day-to-day engine iteration:
  - macOS: `cmake --preset macos-debug`, then `cmake --build --preset macos-build-all --target IRGame`
  - Linux: `cmake --preset linux-debug`, then `cmake --build --preset linux-build-all --target IRGame` (run `scripts/bootstrap_linux.sh` first on Debian/Ubuntu)
  - Windows (MinGW/MSYS2): `cmake --preset windows-debug`, then `cmake --build --preset windows-build-all --target IRGame`
- `IRREDEN_USER_PROJECTS` still exists for advanced setups where you want to attach additional external CMake projects without using `creations/game/`.

---

## Style and Naming

| Context           | Convention                    |
|-------------------|-------------------------------|
| Private members   | `m_` prefix only; no trailing `_` |
| Public members    | trailing `_`                  |
| Components        | `C_` prefix                   |
| Enum values       | `SCREAMING_SNAKE_CASE`        |
| Compute shaders   | `c_` prefix                   |
| Vertex shaders    | `v_` prefix                   |
| Fragment shaders  | `f_` prefix                   |
| Geometry shaders  | `g_` prefix                   |

**Naming style:**
- Prefer descriptive variable names over abbreviations. A longer name that reads clearly is better than a short one that requires context to decode. For example, prefer `viewCenterIso` over `vcIso`, `minimapCenter` over `mmC`, `isCullingFrozen` over `frozen`.
- For non-public helper code that must live in headers, prefer a nested lowercase `detail` namespace under the owning API namespace (`IRSystem::detail`, `IRRender::detail`, `IRCommand::detail`).
- Treat `detail` as a convention marker for implementation helpers, not as part of the intended public API surface.
- Avoid feature-specific helper namespaces such as `MinimapDetail` unless the helper group is intentionally shared across multiple files as a small named submodule.
- Do not use anonymous namespaces in headers except for generated or explicitly documented special cases. Use `detail` for header-only helpers, and keep anonymous namespaces in `.cpp` files when possible.
- If a helper is part of the intentional surface of a header, keep it in the owning namespace instead of moving it into `detail`.

**Logical style:**
- Prefer early return over nested logic.
- Prefer C++ standard library types such as `std::string` over raw C-style buffers when they express the intent clearly. Use fixed C buffers only when a low-level API or measured performance need justifies them.
- Prefer `unique_ptr` over `shared_ptr`. Use `unique_ptr` for the single owner and pass raw pointers to non-owning consumers (e.g. lambda captures, observers). Avoid `shared_ptr` unless true shared ownership is required.
- Raw pointers indicate non-ownership.

---

## Build and Quality

- **Configure:** `cmake --preset linux-debug` (or `macos-debug` / `windows-debug`)
- **Build:** `cmake --build build --target IRShapeDebug` (or `cmake --build --preset linux-build-all`)
- **Format check:** `cmake --build build --target format-check`
- **Format:** `cmake --build build --target format`
- **Lint:** `cmake --build build --target lint` (includes naming checks)

Requires clang-format and clang-tidy.

---

## Architectural Notes

- **Header-only prefabs** – No separate compilation; prefabs compile when included by a creation.
- **Creation-specific systems** – Creations choose which systems to include and register. There is no global system list.
- **Systems are ECS entities** – `SystemId` is an `EntityId`. Systems have components like `C_SystemEvent` and live in the ECS.
- **Implicit position** – `createEntity` always adds `C_PositionGlobal3D` and `C_PositionOffset3D`.
- **Event-driven loop** – Fixed-step update, variable render; pipelines run in input → update → render order.
- **Relations** – ECS supports `CHILD_OF`, `PARENT_TO`, `SIBLING_OF` for hierarchy.
- **Voxel pipeline** – Voxels → Trixel stage 1/2 → Framebuffer; orthographic isometric view.
- **Navigation/pathfinding** – Chunked A\* lives in `engine/math/` (types) and `engine/prefabs/irreden/update/` (query functions, components, systems).
- **Profiling** – `IrredenEngineProfile` wraps easy_profiler.
- **Video capture** – `IrredenEngineVideo` provides `command_take_screenshot` and `command_toggle_recording`.
