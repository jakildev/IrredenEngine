---
paths:
  - "engine/prefabs/**/system_*.{hpp,cpp}"
  - "engine/system/**"
  - "creations/**/system_*.{hpp,cpp}"
---

# System state lives in SystemParams, never function-local static

Rule:

> **Never** use function-local `static` for *mutable* or *system-owned* state inside a system tick or its `create()` function. Use `SystemParams` instead.

Allowed: `static constexpr`, `static const` for genuine compile-time constants. Those are program constants, not system state.

## Why this matters

Function-local `static` for system state is broken on four axes:

1. **Hidden state** — not visible to ECS inspectors, system-walking tools, or anyone reading the prefab catalog.
2. **Lifetime mismatch** — persists for program lifetime, doesn't free when the system entity is destroyed. Every reload leaks.
3. **Single-instance assumption** — all instances of `System<X>` share the same statics. Multi-instance use silently cross-talks.
4. **Conflicts with the ECS philosophy** — "everything on an entity" is the design; statics are the back door.

The performance argument doesn't hold either: `SystemParams` has the same per-tick access cost as `static`. Capture the params pointer once at `create()` time, pass it into lambdas by value — the pointer lookup happens once, not per tick.

## Canonical SystemParams pattern

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

Notes:

- Allocate a `std::make_unique<MyParams>()`, capture its `.get()` pointer **before** the move.
- The lambdas capture `p` by value. `p` outlives the lambdas because `setSystemParams` transfers ownership to the system entity.
- The system entity owns the params; both are destroyed together when the system is destroyed.
- **Don't store raw references to params across frames** — if the system is recreated (e.g. via reload), the pointer is invalid.

## Three valid TICK function signatures

`createSystem<Components...>` detects the signature at compile time via `std::is_invocable_v<>`:

```cpp
// 1. Per-component (most common — pick this by default)
createSystem<C_Velocity3D, C_VelocityDrag>(
    "VelocityDrag",
    [](C_Velocity3D& velocity, C_VelocityDrag& drag) {
        // deltaTime(UPDATE) is the tick's dt
    });

// 2. Per-entity-id (when the system truly needs the entity id)
createSystem<C_NavAgent, C_MoveOrder>(
    "GridPathfind",
    [](EntityId id, C_NavAgent& agent, C_MoveOrder& order) { ... });

// 3. Per-archetype / batch (bulk-processing, SIMD, sort, partition)
createSystem<C_NavAgent>(
    "GridBake",
    [](const Archetype& arch,
       std::vector<EntityId>& ids,
       std::vector<C_NavAgent>& agents) { ... });
```

## beginTick / endTick contract

- `functionBeginTick` fires **once per pipeline execution** before any per-entity ticks. **Signature: `void()`.** No `Archetype&`, no component params. Good for frame-scoped setup.
- `functionEndTick` fires once after all per-entity ticks. **Same `void()` signature.** Good for teardown, GPU upload, swap.
- **Begin/End tick runs even if zero entities match.** Check `ids.size()` yourself if you care.
- `functionRelationTick` fires per-parent when using `RelationParams<...>`. Takes an `EntityRecord` so you can walk the relation.

## Live deviations

Files that still use function-local `static` for system-owned state are tracked in `.fleet/status/system-static-deviations.md` (queue-manager-owned). Don't add new violations; migrate when you're already touching one of the listed files. **Active known violations as of this rule landing:**

- `engine/prefabs/irreden/render/systems/system_entity_canvas_to_framebuffer.hpp:41-43` — `getInstances()` returns a function-local static `std::vector<CanvasInstance>&`.
- `engine/prefabs/irreden/audio/systems/system_rhythmic_launch.hpp:29` — static unordered_map for platform cache.
- `engine/prefabs/irreden/update/systems/system_gravity.hpp:17` — static singleton instance.
- `engine/prefabs/irreden/update/systems/system_animation_color.hpp:25-26` — two static animation clip caches.

Each should be migrated to `SystemParams`. Refactor when touching the file for other reasons; don't delay other work to migrate aggressively.
