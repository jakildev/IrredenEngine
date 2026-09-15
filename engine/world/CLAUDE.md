# engine/world/ — the runtime root

`World` (`include/irreden/world.hpp`) is the one-per-process object that
owns every manager, runs the loop, and tears everything down. It is
constructed by `IREngine::init()`; nothing else constructs one. Rationale for
the persistence rules below:
[`docs/design/world-snapshot-persistence.md`](../../docs/design/world-snapshot-persistence.md);
streaming design: [`docs/design/world-streaming.md`](../../docs/design/world-streaming.md).

## Manager lifetime

- `World` owns every manager as a member in dependency order; each manager's
  own ctor stamps its module global and its dtor clears it. `World` assigns
  none of them — **member order IS the set/clear order**. Adding a manager
  means inserting it at the right point in the member list; the dtor order
  follows. Pattern catalog:
  [`.claude/rules/cpp-globals.md`](../../.claude/rules/cpp-globals.md);
  creation-side contract: [`engine/CLAUDE.md`](../CLAUDE.md) §"Manager globals".
- `LuaScript` leads the manager block so `sol::state` outlives
  `EntityManager` (archetype columns can hold `sol::object` refs).
  `JobManager` follows `SystemManager` and consumes only `WorldConfig`
  ([`engine/job/CLAUDE.md`](../job/CLAUDE.md)).
- Reach managers through the `IR<Module>::get*Manager()` free functions;
  never store a manager reference or `g_*` pointer in anything that can
  outlive the loop (a background `std::thread` capturing `g_renderManager`
  crashes at shutdown).
- `World` owns managers, not game state: no `setPlayer` / `setCameraPosition`
  on it — those are components.
- **Release GPU/GL resources in `end()`, never in `~World()`.** `end()` runs
  inside `gameLoop()` with the context provably live and already drives
  `destroyAllEntities()` for `onDestroy` GPU frees; the dtor is a no-op safety
  net. `IREngine::gameLoop()` resets `g_world` as soon as the loop returns,
  but two paths skip that reset — the loop's catch block calls `end()` and
  rethrows, and an owner that constructs `World` directly picks its own
  destruction point — so treat the reset as defense in depth.

`gameLoop()`: `executePipeline(INPUT)` → `executePipeline(UPDATE)` while
`TimeManager::shouldUpdate()` → `executePipeline(RENDER)` → swap + pacing.

## Lua wiring

`setupLuaBindings(std::vector<LuaBindingRegistration>)` runs before
`gameLoop()`; each registration mutates `LuaScript`'s `sol::state`, so
creations register enum/type/component bindings there, before the first
script runs. `runScript(fileName)`: a bare filename resolves from
`ExeDir/<ExeStem>/`, a path with a directory component from cwd.

## Init-affecting runtime params

A parameter that must land **before** any manager is constructed
(`IRRender::VoxelPoolConfig` sizing, read by `RenderManager`'s ctor) lives in
the same `config = { ... }` table of the creation's `config.lua` as the
`WorldConfig` fields; `IREngine::init` applies it in a non-fatal pre-init
pass (`IREngine::detail::applyPreInitLuaConfig`, `engine/engine.cpp`).
Missing field, missing table, or missing file → the consumer's compiled-in
default.

```lua
config = {
    -- ... WorldConfig fields (init_window_width, ...) ...
    voxel_pool_edge = 128,   -- default 64
}
```

- **Adding one:** extend `applyPreInitLuaConfig` to read the field, apply it
  to its consumer, and log the override at INFO; document it here and in the
  consuming module's `CLAUDE.md`. Never a CLI flag for the same purpose
  (`creations/demos/CLAUDE.md` §"Conventions", "No runtime arguments").
- `WorldConfig` fields are what `World` itself reads at construction; the
  pre-init pass covers what must precede `WorldConfig`'s consumers. One
  source of truth per file.

## Chunk residency

`IRWorld::ChunkResidencyManager` (`include/irreden/world/chunk_residency.hpp`)
is the resident set + per-chunk voxel sub-pool + entity manifest. **Not owned
by `World`** — a creation that opts into streaming constructs one; a
single-chunk creation never sees it. Config knobs, the eviction / prefetch /
deferred-upload behavior, and `FrameStats` are documented on the header.
Chunk-coordinate utilities: [`engine/prefabs/irreden/world/`](../prefabs/irreden/world/).
`IRWorld::ChunkVoxelDiskPersistence` (`chunk_persistence.hpp`) is the
per-chunk `.vxs` save/load wired via `Config::persistence_`; it persists a
chunk's voxel slice only, never entities.

### Chunk mutation must route through `markChunkDirty`

Any write to a chunk-owned `VoxelPoolAllocation` (the slice behind
`ChunkResidencySlot::poolAllocation_`) — and any entity attach / detach /
migrate under streaming — calls `ChunkResidencyManager::markChunkDirty(key)`
immediately after. The dirty bit is what eviction and `flushPendingSaves()`
consult; a missed call silently skips the save and the chunk reverts on
re-resident, which single-chunk creations never observe. `slot->dirty_` is
private (`isDirty()` is the read side). New mutation paths — voxel-pool
write, entity move, component write within `ownedEntities_` — route through
it; the renderer-side pointer is
[`engine/render/CLAUDE.md`](../render/CLAUDE.md) at the voxel-pool section.

## World snapshot (`IRWS`)

`IRWorld::saveWorld` / `loadWorld` (`world_snapshot.hpp`) is the
**entity-level** save; mechanism (chunk layout, projection walk, load phases,
version dispatch) is on the headers. Format contract:
[`engine/asset/CLAUDE.md`](../asset/CLAUDE.md) §"Binary-format contracts".
Three author-facing contracts:

- **Load contract:** `IREntity::resetGameplay()` at a frame boundary, then
  `loadWorld`. Entity ids restore exact; a same-world double-save is
  byte-identical; every failure is a recoverable `IRAsset::BinaryStatus` with
  **zero** world mutation; unknown chunks and unresolvable component names
  skip with counts.
- **GPU state:** `loadWorld` restores CPU data only. The caller registers
  `SEED_STAGED_VOXELS` in its UPDATE pipeline (or calls `attachToCanvas`) to
  move loaded `C_VoxelSetNew`s from staged mode into pool spans;
  `engine/world` does not depend on the voxel/render prefabs.
  `creations/demos/persist_roundtrip` is the reference.
- **Debug dump:** `IR_PERSIST_DUMP` (env flag) emits a `.json.txt` after the
  binary; the binary is byte-identical flag-on or flag-off.

### New-component contract

`SaveTrait<C>` (`save_trait.hpp`) has no default: an engine component with
neither `IR_SAVE_OPT_IN(Type, Version)` nor `IR_SAVE_OPT_OUT(Type)` in
`save_component_inventory.hpp` fails the build. **Opt-out-by-omission is
forbidden.** Adding an engine component means:

- one `IR_SAVE_OPT_IN` / `IR_SAVE_OPT_OUT` line with its include, and an
  `AllEngineComponents` entry, in `save_component_inventory.hpp`. The include
  block sorts alphabetically by full path (`simplify` check 16).
  `cmake/run_save_inventory_population_check.cmake` (part of `header-checks`)
  catches a type omitted from the table entirely.
- a templated component with several instantiations gets ONE representative
  entry (see the comment beside `C_SystemEvent<IRSystem::TICK>` there).
- `kSaveVersion` lives on the trait, not the struct: the snapshot serializes
  the schema `SaveSerialize<C>` defines, not the in-memory layout.

`save_component_inventory.hpp` includes every component header. Only
snapshot TUs and `test/world/save_trait_test.cpp` include it — never a
widely-included header.

### Process-default registry

`makeDefaultSaveRegistry()` (`src/world_default_registry.cpp`) walks
`AllEngineComponents`; its membership is **derived, never curated** — do not
add per-component `register` lines (`test/world/save_serializers_test.cpp`
asserts `size() == countOptIns<AllEngineComponents>()`). Every opted-in entry
instantiates `SaveSerialize<C>`, so an opt-in without a serializer is a build
error. Rules for the serializer:

- **Heap-owning component → `SaveSerialize<C>` specialization** in its
  domain's `engine/prefabs/irreden/<domain>/save_serializers_<domain>.hpp`
  (`C_VoxelSetNew` keeps `voxel_set_serialize.hpp`). Those headers are
  included by `world_default_registry.cpp` only, never by component headers.
- **`read` rejects bytes it cannot honestly restore:** re-check every
  invariant the *accessors* rely on (not merely the constructors), one check
  and one message per fault, each dimension of a multi-dimensional value
  separately, and return `BinaryIOError::UnknownTag`. A guard tighter than
  the type gets a debug-only `IR_ASSERT` mirror in `write`; a guard that only
  re-checks what the type enforces needs none. Reference shapes:
  `SaveSerialize<C_Cycle>`, `SaveSerialize<C_TrianglesOnlySet>`.
- **A component that cannot honestly round-trip opts OUT** with a comment
  (callables with no authored identity, `sol::protected_function` refs).
  Never substitute a default on load. Prefer storing the authored key (an
  enum resolved per tick) over the resolved callable, so the component stays
  trivially copyable and needs no serializer.
- **Any TU that builds a registry includes `save_component_inventory.hpp`.**
  Without the specializations in scope every `registerComponent<C>` no-ops
  and you get an empty registry that saves an empty world without erroring.
- **Never write an explicit `SaveSerialize<C>` for a trivially-copyable
  component.** A TU missing the header binds silently to the raw-image arm —
  an ODR violation with no diagnostic. Need a hand-written layout? Make the
  component non-trivially-copyable or route the exception through the
  inventory.
- **Migration:** a retired `kSaveVersion` gets a `SaveMigration<C>` reader
  (`save_migration.hpp`) — direct per-version, never chained, the current
  version not listed. A disk version below current with no reader is a hard
  `MigratorMissing`; above current is `VersionTooNew`; an unknown name skips.
- **Every new opted-in component adds a round-trip case to
  `test/script/lua_world_snapshot_test.cpp`** through the `IRPersist` Lua
  surface; a serializer unit test alone leaves the wiring unverified.

The registry is built fresh per call (never per-frame), so its session-local
`ComponentId`s always match the live `EntityManager`.

## Gotchas

- **`m_waitForFirstUpdateInput` / `m_startRecordingOnFirstInput`** hold the
  sim / video capture until the first key press. If recording is not
  starting, check these first. `m_waitForFirstUpdateInput` is force-disarmed
  when auto-capture is active (`--auto-screenshot`, the GUI-test path);
  `m_startRecordingOnFirstInput` stays armed.
- **The auto-capture block in `gameLoop()` stays above the priming
  `update()`.** `enableFixedStep()` zeroes the UPDATE lag accumulator and
  `endEvent<UPDATE>()` decrements it unconditionally, so a priming tick before
  the reset leaves every captured frame reading `IRTime::tick()` one high
  ([`engine/time/CLAUDE.md`](../time/CLAUDE.md) §"Gotchas", the
  `enableFixedStep()` bullet). The constraint is on the block as a whole:
  moving only the disarm below the priming call self-cancels; hoisting the
  priming call above `enableFixedStep()` breaks the capture contract, and no
  test covers it.
