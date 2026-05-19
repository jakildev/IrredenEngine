# engine/world/ — the runtime root

The `World` class is the singleton that owns every manager, brings the loop
up, and tears it down. There is exactly one `World` per process, constructed
by `IREngine::init()` and destroyed at shutdown.

## Entry point

`engine/world/include/irreden/world.hpp` — declares `class World`.

Most code never touches `World` directly. It accesses managers via the
`IR<Module>::get*Manager()` free functions in each module's `ir_*.hpp`
header, which reach through the global pointers `World` sets up at
construction time.

## Responsibilities

`World(const char* configFileName)`:

1. Parses `configFileName` into a `WorldConfig` (resolution, FPS, target
   window size, MIDI device, video-capture defaults, etc.).
2. Constructs every manager in dependency order:
   `IRGLFWWindow` → `LuaScript` → `EntityManager` → `SystemManager` →
   `InputManager` → `CommandManager` → `RenderingResourceManager` →
   `RenderManager` → `AudioManager` → `TimeManager` → `VideoManager`.
   `LuaScript` leads the manager block so `sol::state` outlives
   `EntityManager` — archetype columns can hold `sol::object` refs from
   Lua-defined components (T-100), and C++ destructs members in reverse
   declaration order.
3. Sets the globals: `g_entityManager = &m_entityManager;`, etc.
4. Calls `initEngineSystems()`, `initIRInputSystems()`,
   `initIRUpdateSystems()`, `initIRRenderSystems()` to register the
   engine-provided prefab systems and assign them to pipelines.
5. Runs any Lua startup scripts the creation registered via
   `IREngine::registerLuaBindings`.

`gameLoop()`:

- Enters the fixed-step outer loop.
- Each iteration: `executePipeline(INPUT)` → `executePipeline(UPDATE)`
  (one or more times, driven by `TimeManager::shouldUpdate()`) →
  `executePipeline(RENDER)`.
- `IRGLFWWindow::swapBuffers()` + frame pacing at the end.

Destructor:

- Clears the global pointers.
- Destroys managers in reverse order of construction.

## Lua wiring

`setupLuaBindings(std::vector<LuaBindingRegistration>)` is called before
`gameLoop()`. Each registration is a callback that mutates `LuaScript`'s
`sol::state`. Creations use this to register their enum/type/component
bindings before the first Lua script runs.

`runScript(const char* fileName)` loads and executes a Lua file. Bare
filenames resolve from `ExeDir/<ExeStem>/`; paths with a directory component
resolve from cwd.

## Init-affecting runtime params

Runtime parameters that must be applied **before** any manager is
constructed (today: `IRRender::VoxelPoolConfig` sizing, which the
`RenderManager` reads at construction time) live in the same
`config = { ... }` table as the `WorldConfig` fields, in the same
`config.lua`. `IREngine::init` runs a small pre-init pass that loads
the file and applies these fields before constructing the `World`.

The canonical pattern from a creation's side is therefore **nothing** —
the demo's `main()` just calls `IREngine::init(argv[0])` and the
engine handles the rest. The override lives in the creation's own
`config.lua`:

```lua
config = {
    -- ... standard WorldConfig fields (init_window_width, etc) ...
    voxel_pool_edge = 128,   -- override the default 64³ voxel pool
}
```

Missing field → consumer's compiled-in default (`VoxelPoolConfig::kDefaultEdge`
= 64); missing `config` table or missing `config.lua` → same. The pre-init
pass is non-fatal.

**Adding a new init-affecting param.** Extend
`IREngine::detail::applyPreInitLuaConfig` in `engine/engine.cpp` to read
the new field, apply it to its consumer, and log the override at INFO so
startup logs surface non-default values. Document the field here in the
list above and in the consuming module's `CLAUDE.md`. Do not add a CLI
flag for the same purpose — `creations/demos/CLAUDE.md` "No runtime
arguments" forbids it; the Lua config is the single canonical surface.

**vs. `WorldConfig` fields.** `WorldConfig` covers params that World
itself reads at construction time (`init_window_width`, `fit_mode`,
profiling toggles, etc.); the pre-init pass covers params that need to
land **before** `WorldConfig`'s own consumers fire. Both read from the
same `config = { ... }` table — there is one source of truth per file.

## Gotchas

- **Manager lifetime is bounded by `World`.** `g_entityManager` and friends
  are set in the ctor and cleared in the dtor. Don't store references that
  outlive the loop — e.g. a `std::thread` background task that captures
  `g_renderManager` will crash at shutdown.
- **Initialization order matters.** `SystemManager` depends on
  `EntityManager`, `RenderManager` depends on `IRGLFWWindow`, etc. If you
  add a new manager, insert it at the right point in the chain and update
  the dtor order.
- **No `setPlayer` / `setCameraPosition` API on `World`.** Those are ECS
  components. `World` owns *managers*, not game state.
- **`m_waitForFirstUpdateInput` / `m_startRecordingOnFirstInput`** delay
  video recording until the first input arrives — used to keep capture
  clips from starting mid-loading-screen. If video recording is not
  starting, check these flags first.

