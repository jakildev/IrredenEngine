# engine/world/ — the runtime root

The `World` class is the singleton that owns every manager, brings the loop
up, and tears it down. There is exactly one `World` per process, constructed
by `IREngine::init()` and destroyed at shutdown.

## Entry point

`engine/world/include/irreden/world.hpp` — declares `class World`.

## Chunk residency (Epic E)

`engine/world/include/irreden/world/chunk_residency.hpp` declares
`IRWorld::ChunkResidencyManager` — the resident-set + per-chunk voxel
sub-pool + entity manifest. **Not** owned by `World` — creations that
opt into streaming construct one explicitly. Single-chunk creations
ignore it entirely (zero-overhead). Companion chunk-coord utilities
live in [`engine/prefabs/irreden/world/`](../prefabs/irreden/world/);
full design contract in
[`docs/design/world-streaming.md`](../../docs/design/world-streaming.md).

`engine/world/include/irreden/world/chunk_persistence.hpp` declares
`IRWorld::ChunkDiskPersistence` — per-chunk `.vxs` save/load under a
`<saveRoot>/chunks/` directory. One file per chunk; filename embeds
the signed chunk coord (e.g. `+00003_-00007_+00011.vxs`). When wired
on `ChunkResidencyManager::Config::persistence_`, the manager loads
the chunk slice from disk on first `requestResident` and saves dirty
chunks on `requestEvict`. `flushPendingSaves()` is the editor's
save-all hook. Synchronous in v1; E3 lifts the same calls into an
async worker pool without changing the surface. Entity-level state
(components beyond the chunk's voxel pool) belongs to the parallel
world-snapshot path (#199), not this layer.

### Eviction policy (E2)

`ChunkResidencyManager::beginFrame(vec3 cameraWorldVoxel)` recomputes
`distanceVoxels_` for every slot and marks slots beyond
`R_prefetch + R_hysteresis` as `EVICTING`. `endFrame()` processes
`EVICTING` slots (saves dirty ones via persistence, deallocates pool
slices via the `PoolDeallocator` callback, erases the slot) then
enforces the budget cap — evicting furthest-from-camera slots with
LRU tie-breaking until `residentChunkCount() <= maxResidentChunks_`.

Config knobs on `ChunkResidencyManager::Config`:
- `maxResidentChunks_` (default 256)
- `viewRadiusVoxels_` (default 128.0f — matches the light-volume window)
- `prefetchRadiusVoxels_` (default 256.0f)
- `hysteresisVoxels_` (default 32.0f = one chunk edge — prevents thrashing)

`PoolDeallocator` is the deallocation counterpart to `PoolAllocator`:
production wires it to return the pool slice to `C_VoxelPool`'s
free-list via `deallocateVoxels(startIndex, size)`. The E1 skeleton
leaked allocations on evict; E2 closes that path.

`FrameStats` (via `frameStats()`) reports `evictedThisFrame_`,
`loadedThisFrame_`, and `residentCount_` for HUD display and profiling.

### Chunk mutation must route through `markChunkDirty`

> Any code that writes to a chunk-owned `VoxelPoolAllocation` (the
> slice exposed via `ChunkResidencySlot::poolAllocation_`) MUST call
> `ChunkResidencyManager::markChunkDirty(key)` immediately after the
> write. The same rule covers entity attach / detach / migrate when
> a creation opts into streaming.

The dirty bit is consulted at eviction and by `flushPendingSaves()`;
a missed `markChunkDirty` call after a real mutation means the save
is silently skipped and the chunk reverts to its pre-edit state on
re-resident. This is the ECS-footgun class of bug — invisible under
single-chunk creations, fires only after
streaming load surfaces an eviction-then-re-resident cycle.

`ChunkResidencySlot::isDirty()` is the read side; the underlying
field is private with `ChunkResidencyManager` as a `friend`, so
`slot->dirty_ = true` no longer compiles. The manager's
`attachEntity` / `migrateEntity` already self-route through
`markChunkDirty`; the voxel-mutation routing lands when push-at-
mutation uploads (Epic B / #944) wire in.

When you add a new mutation path (voxel-pool write, entity move,
component write within `ownedEntities_`), route it through
`markChunkDirty`. The cross-link from
[`engine/render/CLAUDE.md`](../render/CLAUDE.md) at the voxel-pool
section is the reciprocal pointer for renderer-side authors.

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
   `JobManager` → `InputManager` → `CommandManager` →
   `RenderingResourceManager` → `RenderManager` → `AudioManager` →
   `TimeManager` → `VideoManager`.
   `LuaScript` leads the manager block so `sol::state` outlives
   `EntityManager` — archetype columns can hold `sol::object` refs from
   Lua-defined components (T-100), and C++ destructs members in reverse
   declaration order. `JobManager` slots in after `SystemManager`
   because the worker pool sits below the engine's high-level managers
   (renderer, input, video) and consumes only `WorldConfig` —
   see `engine/job/CLAUDE.md` for the IRJob surface and lifetime
   contract (Phase 1 of the multithreading epic #226).
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

