# engine/system/ — pipeline scheduler

Registers tick-function handlers bound to component archetypes, and executes
them in deterministic order per time event (`INPUT` → `UPDATE` → `RENDER`).
Systems are themselves entities: `SystemId` is an alias for `EntityId`, and a
system's behavior is stored as `C_SystemEvent<TICK>` etc. components on the
system entity.

## Public API

`IRSystem::` exposes: `createSystem<...>()`, `registerPipeline()`,
`executePipeline()`, system tag helpers, and per-system-parameters.

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

### Don't use function-local `static` for system state

Function-local `static` for system-owned state is an anti-pattern.
Use `SystemParams` instead.

**Why it's wrong:**
- Hidden state — not visible to ECS inspectors or system-walking tools.
- Lifetime mismatch — persists for program lifetime, doesn't free when the
  system entity is destroyed.
- Single-instance assumption — all instances of `System<X>` share the same
  statics; future multi-instance use silently cross-talks.
- Conflicts with the ECS "everything on an entity" philosophy.

**Why the perf argument doesn't hold:** the canonical `SystemParams` pattern
has the same per-tick access cost as `static`. Capture the pointer once at
`create()` time and pass into lambdas by value — the pointer lookup happens
once, not per tick.

```cpp
SystemId create() {
    SystemId myId = ...;
    setSystemParams(myId, std::make_unique<MyParams>());
    auto* p = getSystemParams<MyParams>(myId);   // once, at create
    return createSystem<...>(
        "Name",
        [p](C_Foo& foo) { p->bar += foo.x; },
        [p]()           { p->bar = 0.0f; },
        [p]()           { /* end-of-tick using p */ }
    );
}
```

**Exception:** truly invariant data — `constexpr` integer constants,
named-resource pointers fetched once at engine init that never change — is
fine as `static`. Those are program constants, not system state. The rule
applies to *mutable* or *system-owned* state.

**Current deviations** (migration tracked in T-065):
- `engine/prefabs/irreden/render/systems/system_trixel_to_trixel.hpp`
- `engine/prefabs/irreden/render/systems/system_lighting_to_trixel.hpp`
- `engine/prefabs/irreden/render/systems/system_shapes_to_trixel.hpp`
- `engine/prefabs/irreden/render/systems/system_build_occupancy_grid.hpp`
- `engine/prefabs/irreden/render/systems/system_fog_to_trixel.hpp`
- `engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp`
- `engine/prefabs/irreden/render/systems/system_text_to_trixel.hpp`
- `engine/prefabs/irreden/render/systems/system_compute_sun_shadow.hpp`
- `engine/prefabs/irreden/render/systems/system_sprites_to_screen.hpp`
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp`
- `engine/prefabs/irreden/render/systems/system_framebuffer_to_screen.hpp`
- `engine/prefabs/irreden/render/systems/system_compute_voxel_ao.hpp`

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

