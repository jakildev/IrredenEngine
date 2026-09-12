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

`engine/` is a set of static libraries — `asset`, `audio`, `command`,
`common`, `entity` (IRECS, the archetype ECS), `input`, `math`, `prefabs`
(header-only components / systems / commands / entities), `profile`,
`render`, `script` (LuaJIT 2.1 + sol2), `system`, `time`, `video`,
`window`, `world` — beside `creations/` (demos, editors, the gitignored
private `game/`), `cmake/`, `docs/`, and `test/`. Each module has a
`CLAUDE.md`.

`engine/include/irreden/ir_engine.hpp` is the single top-level entry point:
it creates the `World`, sets `cwd` to the exe directory, and derives the
scripts directory from the exe stem (`IRMidiPolyrhythm/`). Each module
exposes its own `ir_<module>.hpp` (`ir_system.hpp`, `ir_entity.hpp`,
`ir_math.hpp`, …); creations include those, never internal module headers.

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
   struct C_MoveOrder {
       IRMath::ivec3 targetCell_;
       C_MoveOrder() : targetCell_{0, 0, 0} {}
       C_MoveOrder(IRMath::ivec3 targetCell) : targetCell_{targetCell} {}
   };
   }
   ```
2. **Systems** — `IRSystem::System<SYSTEM_NAME>` specializations with a
   static `create()` returning `SystemId`, at
   `engine/prefabs/irreden/<domain>/systems/system_*.hpp`. A new prefab
   system first adds its name to the `SystemName` enum in
   `ir_system_types.hpp`. Three tick signatures — per-component,
   per-entity-id (`EntityId id, C_A&, C_B&`), and per-archetype batch
   (`const Archetype&, std::vector<EntityId>&, std::vector<C_A>&`); the
   per-component form:
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
   Signatures, begin/end/relation ticks, per-system parameters:
   `engine/system/CLAUDE.md`.
3. **Entities** — `IREntity::createEntity(C_A{...}, ...)`;
   `createEntityBatch(n, ...)` / `createEntityBatchWithFunctions(...)` for
   per-index init. `createEntity` always adds `C_PositionGlobal3D`.

`SystemId` is an alias for `EntityId` — systems are entities in the ECS
(with components such as `C_SystemEvent`). Relations `CHILD_OF`,
`PARENT_TO`, `SIBLING_OF` express hierarchy; pass a `RelationParams<>` to
`createSystem` for relation-aware systems. Per-entity `getComponent` in
ticks, deferred entity ops, component-method tiers:
[`CLAUDE-BASELINE.md`](CLAUDE-BASELINE.md),
[`.claude/rules/cpp-ecs.md`](../../.claude/rules/cpp-ecs.md).

Prefabs are header-only (compiled when a creation includes them) and grouped
by domain under `engine/prefabs/irreden/` (`common/`, `update/`, `voxel/`,
`input/`, `render/`, `audio/`, `video/`), one file pattern per kind:

| Kind       | Path pattern                   |
|------------|--------------------------------|
| Components | `*/components/component_*.hpp` |
| Systems    | `*/systems/system_*.hpp`       |
| Commands   | `*/commands/command_*.hpp`     |
| Entities   | `*/entities/entity_*.hpp`      |

`engine/prefabs/irreden/update/nav_query.hpp` (free functions for the
chunked A\* query API) sits directly in `update/`.

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

| Component            | Type    | Purpose                                             |
|----------------------|---------|-----------------------------------------------------|
| `C_PositionGlobal3D` | `vec3`  | World-space position (auto-added by `createEntity`) |
| `C_Position3D`       | `vec3`  | General-purpose 3D position                         |
| `C_PositionInt3D`    | `ivec3` | Integer cell / grid position                        |
| `C_Position2DIso`    | `vec2`  | 2D isometric grid position                          |
| `C_Position2D`       | `vec2`  | 2D screen / game-resolution position                |

Per-frame additive offsets (idle bob, gizmo nudges) travel through the
modifier framework's `POSITION_OFFSET_3D` vec3 field and are folded into
`C_PositionGlobal3D` by `APPLY_POSITION_OFFSET` once per UPDATE tick; the
rendered position is whatever `C_PositionGlobal3D` holds after that pass.

### Planes, tiles, navigation, velocity

- `enum class PlaneIso { XY = 0, XZ = 1, YZ = 2 }` selects the 2D plane the
  layout helpers (`layoutGridCentered`, `layoutCircle`, …) place into: `XY`
  is the ground plane (depth along Z), `XZ` depth along Y, `YZ` depth along
  X. `enum class CoordinateAxis { XAxis = 0, YAxis = 1, ZAxis = 2 }`.
- 2D isometric tile basis (`kIHatGridToScreenIso` / `kJHatGridToScreenIso`):
  `iHat = (1.0, 0.5) * objectSize/2`, `jHat = (-1.0, 0.5) * objectSize/2` —
  the standard 2:1 diamond.
- Navigation adds a chunked layer: **world cell** (`ivec3`), **chunk
  coordinate** (`ChunkCoord` = `ivec2`, XY only), **local cell** (`ivec3`);
  conversions `worldCellToChunkCoord`, `worldCellToLocalCell`,
  `localCellToWorldCell` in `nav_types.hpp`. Chunked A\* types live in
  `engine/math/`; the query functions, components, and systems in
  `engine/prefabs/irreden/update/`.
- `C_Velocity3D` is blocks per second; integrators use
  `IRTime::deltaTime(IRTime::UPDATE)`.

## Lua integration

LuaJIT 2.1 + sol2. C++ sets up systems and pipelines; Lua drives entity
creation, configuration, and runtime logic. A Lua creation carries
`main_lua.cpp` (entry: `registerLuaBindings`, `IREngine::init`, loop),
`lua_bindings.{hpp,cpp}`, `lua_component_pack.hpp`, `config.lua`,
`main.lua`, `scripts/`, and `lua_defs/irreden_api.lua` (LSP definitions,
not loaded at runtime). `creations/demos/default/` is the reference.

1. **`config.lua`** defines a `config = { ... }` table parsed at startup
   (window size, resolution, fit mode, MIDI device, video capture).
2. **`lua_component_pack.hpp`** lists the components to bulk-register,
   including each one's `*_lua.hpp` variant:
   `using LuaComponentPack = std::tuple<C_RhythmicLaunch, ...>;`
3. A **`*_lua.hpp` variant** opts a component in by specializing
   `kHasLuaBinding<T> = true` and implementing `bindLuaType<T>(LuaScript&)`.
4. **`lua_bindings.cpp`** calls `IREngine::registerLuaBindings(lambda)`
   before `IREngine::init`; inside: `luaScript.registerEnum<>()`,
   `luaScript.registerType<T, Constructors...>(name, key, &T::member, ...)`,
   `luaScript.registerTypesFromTraits<C_A, C_B, ...>()`.
5. **`main.lua`** loads sub-scripts with `dofile(SCRIPT_DIR .. "name.lua")`;
   `SCRIPT_DIR` is `ExeDir/ExeStem/scripts/`.

Namespaces exposed to scripts: `IREntity.*`, `IRAudio.*`, `IRInput.*`,
`IRPhysics.*` (registered via calls like
`luaScript.registerCreateEntityFunction(...)`).
`IREngine::resolveScriptPath(filename)`: bare filenames resolve from
`ExeDir/ExeStem/`; paths with a directory component resolve from cwd.

## Creations

1. Add `creations/demos/<name>/` (or `editors/`), with a `CMakeLists.txt`
   that `add_executable(...)`s and links `PUBLIC IrredenEngine`, and
   register it in the parent `CMakeLists.txt`.
2. C++-only: `main.cpp`. Lua-driven: the file set above.
3. Register pipelines in `IREngine::init`, including only the components
   and systems the creation needs.

A private game lives at `creations/game/`; the root `CMakeLists.txt`
auto-adds it when `creations/game/CMakeLists.txt` exists, so it builds in
the same root graph as the demos (shared code in a library target such as
`IRGameLib`, the runnable in `IRGame`, helper targets `IRGameAssets` /
`IRGameRun` as needed):

- macOS: `cmake --preset macos-debug`, then
  `cmake --build --preset macos-build-all --target IRGame`
- Linux: `cmake --preset linux-debug`, then
  `cmake --build --preset linux-build-all --target IRGame`
  (`scripts/bootstrap_linux.sh` first on Debian/Ubuntu)
- Windows (MinGW/MSYS2): `cmake --preset windows-debug`, then
  `cmake --build --preset windows-build-all --target IRGame`

`IRREDEN_USER_PROJECTS` attaches additional external CMake projects without
using `creations/game/`.
