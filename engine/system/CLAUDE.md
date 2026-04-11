# engine/system/ — pipeline scheduler

Registers tick-function handlers bound to component archetypes, and executes
them in deterministic order per time event (`INPUT` → `UPDATE` → `RENDER`).
Systems are themselves entities: `SystemId` is an alias for `EntityId`, and a
system's behavior is stored as `C_SystemEvent<TICK>` etc. components on the
system entity.

## Entry point

`engine/system/include/irreden/ir_system.hpp` — exposes `IRSystem::`:
`createSystem<...>()`, `registerPipeline()`, `executePipeline()`, system
tag helpers, and per-system-parameters.

## Key types

- **`SystemId`** — `EntityId` alias. Systems are ECS entities.
- **`SystemEvent`** enum (`ir_system_types.hpp`) — `BEGIN_TICK`, `TICK`,
  `END_TICK`, `RELATION_TICK`, `START`, `STOP`. These are the dispatch
  slots a system can hook.
- **`SystemName`** enum (`ir_system_types.hpp`) — predefined system type
  identifiers (e.g. `VELOCITY_3D`, `VOXEL_TO_TRIXEL_STAGE_1`). Prefab
  systems specialize `template <> struct System<SYSTEM_NAME>::create()`.
  **Any new prefab system must add its name to this enum first or the
  specialization won't link.**
- **`C_SystemEvent<T>`** — component specialization holding the function
  pointer and archetype for each dispatch slot.
- **`RelationParams<Components...>`** — passed to `createSystem` when the
  system needs a `RELATION_TICK` for a specific relation/component combo.

## Three valid TICK function signatures

`createSystem<Components...>` detects the signature at compile time via
`std::is_invocable_v<>`:

```cpp
// 1. Per-component (most common)
createSystem<C_Velocity3D, C_VelocityDrag>(
    "VelocityDrag",
    [](C_Velocity3D& velocity, C_VelocityDrag& drag) {
        // deltaTime(UPDATE) is the tick's dt
    });

// 2. Per-entity-id (when you need the id)
createSystem<C_NavAgent, C_MoveOrder>(
    "GridPathfind",
    [](EntityId id, C_NavAgent& agent, C_MoveOrder& order) { ... });

// 3. Per-archetype / batch
createSystem<C_NavAgent>(
    "GridBake",
    [](const Archetype& arch,
       std::vector<EntityId>& ids,
       std::vector<C_NavAgent>& agents) { ... });
```

Pick (1) by default. Go to (2) only when the system truly needs the id.
Go to (3) for bulk-processing opportunities (SIMD, sort, partition).

## Begin/End/Relation ticks

- `functionBeginTick` — fires **once per pipeline execution** before any
  per-entity ticks. No arguments. Good for frame-scoped setup.
- `functionEndTick` — fires once after all per-entity ticks. Good for
  teardown, gpu upload, swap.
- `functionRelationTick` — fires per-parent when using
  `RelationParams<...>`. Takes an `EntityRecord` so you can walk the
  relation.
- **Begin/End tick runs even if zero entities match.** Check `ids.size()`
  yourself if you care.

## Per-system parameters

If a system needs persistent state beyond components (e.g. a GPU buffer
handle, an accumulator), allocate a `SystemParams` subclass and attach:

```cpp
setSystemParams(systemId, std::make_unique<MyParams>(...));
auto& params = getSystemParams<MyParams>(systemId);
```

The params are owned by the system entity and freed when the system is
destroyed. **Do not store raw references to params across frames** — if the
system is recreated (e.g. via reload), the pointer is invalid.

## Pipelines

```cpp
IRSystem::registerPipeline(IRTime::Events::UPDATE, {
    velocitySystem,
    collisionSystem,
    animationSystem,
});
```

Order in the list is execution order. Systems run sequentially — no
parallelism. A creation registers its pipelines during init; changing them
mid-frame is supported but uncommon.

## Gotchas

- **Archetype mutations in a TICK will skip or revisit entities.** If you
  must add/remove components inside a tick, use the deferred API in
  `IREntity` and let `flushStructuralChanges` run at the end of the
  pipeline.
- **`SystemName` enum is a required registry.** Missing entries cause
  linker errors, not runtime errors — easy to miss if you only run one
  creation's subset of prefab systems.
- **`beginTick`/`endTick` signatures must be `void()`.** No `Archetype&`
  argument; they're node-agnostic.
- **Relation ticks are opt-in.** Only the systems that pass
  `RelationParams<...>` will ever fire `RELATION_TICK`, even if the
  relation exists in the ECS.

## Internal layout

```
engine/system/
├── include/irreden/
│   ├── ir_system.hpp               — public facade
│   └── system/
│       ├── ir_system_types.hpp     — SystemEvent, SystemName, RelationParams
│       ├── system_manager.hpp      — SystemManager template impl
│       └── components/
│           ├── component_system_event.hpp     — C_SystemEvent<T>
│           └── component_system_relation.hpp  — C_SystemRelation
└── src/                             — SystemManager impl, pipeline exec
```
