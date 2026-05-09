# engine/system/ — pipeline scheduler

Registers tick-function handlers bound to component archetypes, and executes
them in deterministic order per time event (`INPUT` → `UPDATE` → `RENDER`).
Systems are themselves entities: `SystemId` is an alias for `EntityId`, and a
system's behavior is stored as `C_SystemEvent<TICK>` etc. components on the
system entity.

## Public API

`IRSystem::` exposes: `createSystem<...>()`, `createSystemDynamic()`,
`replaceSystemBody()`, `registerPipeline()`, `executePipeline()`,
system tag helpers, and per-system-parameters.

## `createSystemDynamic` for runtime-typed systems

`createSystem<Components...>()` is the canonical path — component types
are template parameters, the body's tick signature is detected via
`std::is_invocable_v`, and dispatch is the per-entity-shape
fast path described below.

`createSystemDynamic(name, includeArchetype, excludeArchetype, body)`
is the runtime-typed parallel, used by Lua-defined systems
(`LuaScript::registerSystem`). The component sets are passed as
resolved `IREntity::Archetype` (sets of `ComponentId`) and the body
is a `std::function<void(ArchetypeNode*)>` that fires once per
matched archetype. Per-entity iteration is the body's responsibility,
not SystemManager's — the Lua surface uses this to keep the C++/Lua
boundary at one `sol::function` call per archetype.

Both paths share the same scheduler: `executePipeline` walks
`m_ticks[system].functionTick_`, which is a `std::function<void(
ArchetypeNode*)>` regardless of which factory created the system.

## `replaceSystemBody` for hot-reload

`replaceSystemBody(systemId, body)` swaps the per-archetype tick body of
an existing system in place. The system's `SystemId`, archetype filter,
exclude archetype, `SystemParams`, and pipeline registrations are
unchanged — only the function invoked per matched `ArchetypeNode` is
rebound. In-flight entities continue using the new body on the next
pipeline tick with no special handling.

Used by the Lua-driven hot-reload path (`IRSystem.replaceSystemBody`,
documented in `engine/script/CLAUDE.md`) where the Lua side reseats
the captured `sol::protected_function` inside the dynamic system's
body lambda via a shared reference. Any C++ caller can also use the
free function directly when the body is a plain
`std::function<void(ArchetypeNode*)>`.

Schema-level changes (different include / exclude archetype) are out
of scope — `replaceSystemBody` is the cheap path that avoids entity
migration. To change the archetype, register a new system.

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
handle, an accumulator), allocate a `SystemParams` subclass. Call
`setSystemParams` **after** `createSystem` — the system entity must exist
first. See the canonical example in the section below.

```cpp
// After createSystem returns systemId:
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
    auto paramsOwner = std::make_unique<MyParams>();
    auto* p = paramsOwner.get();    // capture raw ptr before move
    SystemId myId = createSystem<...>(
        "Name",
        [p](C_Foo& foo) { p->bar += foo.x; },
        [p]()           { p->bar = 0.0f; },
        [p]()           { /* end-of-tick using p */ }
    );
    setSystemParams(myId, std::move(paramsOwner));
    return myId;
}
```

**Exception:** truly invariant data — `constexpr` integer constants,
named-resource pointers fetched once at engine init that never change — is
fine as `static`. Those are program constants, not system state. The rule
applies to *mutable* or *system-owned* state.

See `.fleet/status/system-static-deviations.md` (queue-manager-owned;
feature PRs do not edit) for the current list of files still using
function-local `static` for system state.

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

