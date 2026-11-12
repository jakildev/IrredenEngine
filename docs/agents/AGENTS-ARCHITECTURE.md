# Irreden Engine — Architecture reference

The *shape* of the engine: where things live, what owns what, how the pieces
fit. Conventions: [`CLAUDE-BASELINE.md`](CLAUDE-BASELINE.md) and
[`.claude/rules/`](../../.claude/rules/); build: [`BUILD.md`](BUILD.md);
fleet workflow: [`FLEET.md`](FLEET.md).

Irreden is an isometric "pixelatable" voxel content and game engine built
around an archetype-based ECS. Game logic lives in components and systems;
C++ owns systems, pipelines, and bindings; Lua drives configuration and
creation logic.

## Modules and entry points

`engine/` contains the static-library modules and the header-only prefab
library. Each module has a `CLAUDE.md` that owns its local architecture and
conventions. Tracked creations live under `creations/`; the
[creation instructions](../../creations/CLAUDE.md) describe the supported
categories and the visibility boundary for local projects.

`engine/include/irreden/ir_engine.hpp` is the single top-level entry point:
it creates the `World`, sets `cwd` to the exe directory, and derives the
scripts directory from the exe stem (`IRMidiPolyrhythm/`). Each module
exposes its own `ir_<module>.hpp` (`ir_system.hpp`, `ir_entity.hpp`,
`ir_math.hpp`, …). Creations prefer those module entry points; public prefab
component and system headers may be included directly when no umbrella exports
them, while private implementation headers remain off limits.

## The `World` class

`engine/world/include/irreden/world.hpp` — the runtime root, constructed as
a `unique_ptr<World>` by `IREngine::init()`. It owns every manager as a
member in dependency order (`EntityManager`, `SystemManager`,
`InputManager`, `CommandManager`, `RenderManager`,
`RenderingResourceManager`, `AudioManager`, `TimeManager`, `VideoManager`,
`IRGLFWWindow`, `LuaScript`); all subsystem access flows through it.

## ECS: components, systems, entities

1. **Components** — plain data structs, `C_` prefix, `IRComponents`
   namespace, public members with trailing `_`, at
   `engine/prefabs/irreden/<domain>/components/component_*.hpp`:
   ```cpp
   namespace IRComponents {
   struct C_PositionInt3D {
       ivec3 pos_;

       C_PositionInt3D(ivec3 pos)
           : pos_{pos} {}
   };
   }
   ```
2. **Systems** — `IRSystem::System<SYSTEM_NAME>` specializations with named
   member hooks and a static `create()` returning `SystemId`, at
   `engine/prefabs/irreden/<domain>/systems/system_*.hpp`. A new prefab
   system first adds its name to the `SystemName` enum in
   `ir_system_types.hpp`. Three tick signatures — per-component,
   per-entity-id (`EntityId id, C_A&, C_B&`), and per-archetype batch
   (`const Archetype&, std::vector<EntityId>&, std::vector<C_A>&`); the
   per-component form:
   ```cpp
   template <> struct System<VELOCITY_3D> {
       void tick(C_LocalTransform &localXform, const C_Velocity3D &velocity) {
           localXform.translation_ +=
               velocity.velocity_ * vec3(IRTime::deltaTime(IRTime::UPDATE));
       }

       static SystemId create() {
           return registerSystem<VELOCITY_3D, C_LocalTransform, C_Velocity3D>(
               "Velocity3D");
       }
   };
   ```
   Signatures, begin/end/relation ticks, per-system parameters:
   `engine/system/CLAUDE.md`.
3. **Entities** — `IREntity::createEntity(C_A{...}, ...)`;
   `createEntityBatch(n, ...)` / `createEntityBatchWithFunctions(...)` for
   per-index init. `createEntity` always adds `C_LocalTransform` and
   `C_WorldTransform` unless the caller supplies either explicitly.

`SystemId` is an alias for `EntityId` — systems are entities in the ECS
(with components such as `C_SystemEvent`). `CHILD_OF` is the implemented
hierarchy relation; pass a `RelationParams<>` when registering a
relation-aware system. Per-entity `getComponent` in ticks, deferred entity
ops, component-method tiers:
[`CLAUDE-BASELINE.md`](CLAUDE-BASELINE.md),
[`.claude/rules/cpp-ecs.md`](../../.claude/rules/cpp-ecs.md).

Prefabs are header-only (compiled when a creation includes them) and grouped
by domain under `engine/prefabs/irreden/`. Each domain's `CLAUDE.md` owns its
catalog and local patterns; the common file patterns are:

| Kind       | Path pattern                   |
|------------|--------------------------------|
| Components | `*/components/component_*.hpp` |
| Systems    | `*/systems/system_*.hpp`       |
| Commands   | `*/commands/command_*.hpp`     |
| Entities   | `*/entities/entity_*.hpp`      |

## Commands

Input-triggered actions, defined by template specialization like systems;
input modifiers (Shift, Ctrl) are additional arguments:

```cpp
IRCommand::createCommand<IRCommand::ZOOM_IN>(
    InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED, KeyMouseButtons::kKeyButtonEqual
);
```

## Pipelines and execution order

- Pipelines are per event: `IRTime::Events::INPUT`, `UPDATE`, `RENDER`.
- Creations register systems with
  `IRSystem::registerPipeline(event, {systemIds...})`; list order is
  execution order within the event. There is no global system list — each
  creation includes and registers only what it needs.
- Systems run only on entities that have all required components.
- The loop is **input → update → render**: fixed-step update, variable
  render.

## Render Pipeline

**Voxels → Trixel stage 1 → Trixel stage 2 → Trixel-to-Framebuffer →
Framebuffer-to-Screen**, in an orthographic isometric view.

- `TRIXEL_TO_TRIXEL` composites multiple trixel canvases.
- `TEXT_TO_TRIXEL` renders text via `TrixelFont`.
- `CANVAS_DRAW` rasterizes the frame's queued canvas draws (text, voxel
  letters and runs, rectangles) onto any named canvas at a composite depth
  ([design](../design/canvas-draw-layers.md)).
- GLSL shaders live in `engine/render/src/shaders/` with `c_` / `v_` /
  `f_` / `g_` prefixes; the Metal backend mirrors them.

## Coordinate Systems and Math

Vector and matrix types are GLM aliases in `IRMath` (`ir_math_types.hpp`):
`vec2`, `vec3`, `ivec3`, `mat4`, …

### 3D voxel space

- **X** points to the lower-left of the iso view, **Y** to the lower-right.
- **Z** is vertical and **+Z points down** in the rendered view (gravity);
  lower Z renders higher on screen. The ground plane is XY.

Consequences of +Z down: a sun direction (`vec3` toward the sun) always has
`z <= 0` (`IRRender::setSunDirection` asserts it); `Z_FACE` is the voxel's
visual top face and its outward normal in face-shading code is `(0, 0, -1)`;
a floor-anchored shape places its `+halfExtent.z` face against the floor's
`-halfExtent.z` face.

### Isometric projection

`IRMath::pos3DtoPos2DIso`:

```
iso.x = -x + y
iso.y = -x - y + 2z
```

`pos3DtoPos2DScreen` scales by `triangleStepSizeScreen` and applies the
backend's X/Y sign (`IRPlatform::kGfx.screenYDirection_`). Never inline the
equations — call the helpers (`engine/math/CLAUDE.md` §"Isometric
projection — the equations").

### Depth

`distance = x + y + z` (`IRMath::pos3DtoDistance`); the depth axis is
(1,1,1), and higher distance is further from the camera.
`IRMath::isoDepthShift(pos, d)` shifts by `(d, d, d)`, changing depth
without moving the 2D projection.

### Face types

| FaceType | Perpendicular to | Visible side |
|----------|------------------|--------------|
| `X_FACE` | X                | Right        |
| `Y_FACE` | Y                | Left         |
| `Z_FACE` | Z                | Top          |

Face assignment and per-face trixel placement happen in
`c_voxel_to_trixel_stage_1.glsl`.

### Position components

| Component          | Type    | Purpose                                      |
|--------------------|---------|----------------------------------------------|
| `C_LocalTransform` | SQT     | Transform relative to the `CHILD_OF` parent  |
| `C_WorldTransform` | SQT     | Resolved world transform used by consumers   |
| `C_PositionInt3D`  | `ivec3` | Integer cell / grid position                 |
| `C_Position2DIso`  | `vec2`  | 2D isometric grid position                   |
| `C_Position2D`     | `vec2`  | 2D screen / game-resolution position         |

`createEntity` attaches the transform pair. Per-frame additive offsets travel
through the modifier fields `TRANSFORM_TRANSLATION` and `TRANSFORM_SCALE`;
`PROPAGATE_TRANSFORM` composes them with the local and parent transforms into
`C_WorldTransform` before its consumers run.

### Planes, tiles, velocity

- `enum class PlaneIso { XY = 0, XZ = 1, YZ = 2 }` selects the 2D plane the
  layout helpers (`layoutGridCentered`, `layoutCircle`, …) place into: `XY`
  is the ground plane (depth along Z), `XZ` depth along Y, `YZ` depth along
  X. `enum class CoordinateAxis { XAxis = 0, YAxis = 1, ZAxis = 2 }`.
- 2D isometric tile basis (`kIHatGridToScreenIso` / `kJHatGridToScreenIso`):
  `iHat = (1.0, 0.5) * objectSize/2`, `jHat = (-1.0, 0.5) * objectSize/2` —
  the standard 2:1 diamond.
- `C_Velocity3D` is blocks per second; integrators use
  `IRTime::deltaTime(IRTime::UPDATE)`.

## Lua integration

LuaJIT 2.1 + sol2. C++ sets up systems and pipelines; Lua drives entity
creation, configuration, and runtime logic. A Lua creation carries
`main_lua.cpp` (entry: `registerLuaBindings`, `IREngine::init`, loop),
`lua_bindings.{hpp,cpp}`, `lua_component_pack.hpp`, `config.lua`,
`main.lua`, and `scripts/`. `creations/demos/default/` is the reference.

1. **`config.lua`** defines a `config = { ... }` table parsed at startup
   (window size, resolution, fit mode, MIDI device, video capture).
2. **`lua_component_pack.hpp`** includes each component's `*_lua.hpp`
   variant and defines a registration function that calls
   `registerTypesFromTraits<...>()` with the opted-in component types.
3. A **`*_lua.hpp` variant** opts a component in by specializing
   `kHasLuaBinding<T> = true` and implementing `bindLuaType<T>(LuaScript&)`.
4. **`lua_bindings.cpp`** calls `IREngine::registerLuaBindings(lambda)`
   before `IREngine::init`; inside: `luaScript.registerEnum<>()`,
   `luaScript.registerType<T, Constructors...>(name, key, &T::member, ...)`,
   `luaScript.registerTypesFromTraits<C_A, C_B, ...>()`.
5. **`main.lua`** loads sub-scripts with `dofile(SCRIPT_DIR .. "name.lua")`;
   `SCRIPT_DIR` is `ExeDir/ExeStem/scripts/`.

Core bindings expose namespaces including `IRMath`, `IRComponent`, `IREnum`,
`IREntity`, `IRWorld`, and `IRSystem`; creations may register additional
tables and functions such as
`luaScript.registerCreateEntityFunction(...)`.
`IREngine::resolveScriptPath(filename)`: bare filenames resolve from
`ExeDir/ExeStem/`; paths with a directory component resolve from cwd.

## Creations

1. Add a tracked creation in one of the categories documented by
   the [creation instructions](../../creations/CLAUDE.md), with a
   `CMakeLists.txt` that
   `add_executable(...)`s and links `PUBLIC IrredenEngine`, and register it
   in the parent `CMakeLists.txt`.
2. C++-only: `main.cpp`. Lua-driven: the file set above.
3. Register pipelines in `IREngine::init`, including only the components
   and systems the creation needs.

Build and platform setup live in [`BUILD.md`](BUILD.md).
`IRREDEN_USER_PROJECTS` attaches additional external CMake projects to the
engine build.
