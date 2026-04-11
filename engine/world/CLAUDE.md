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
   `EntityManager` → `SystemManager` → `InputManager` → `CommandManager`
   → `TimeManager` → `IRGLFWWindow` → `RenderManager` →
   `RenderingResourceManager` → `AudioManager` → `VideoManager` → `LuaScript`.
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

## Internal layout

```
engine/world/
├── include/irreden/world/
│   ├── world.hpp       — class World
│   └── config.hpp      — WorldConfig struct
└── src/                — gameLoop, manager wiring, init* functions
```
