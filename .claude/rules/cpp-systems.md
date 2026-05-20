---
paths:
  - "engine/prefabs/**/system_*.{hpp,cpp}"
  - "engine/system/**"
  - "creations/**/system_*.{hpp,cpp}"
---

# System state lives on System<N> or in SystemParams, never function-local static

Rule:

> **Never** use function-local `static` for *mutable* or *system-owned* state inside a system tick or its `create()` function. Use the member-on-`System<N>` form (preferred) or the explicit `SystemParams` form.

Allowed: `static constexpr`, `static const` for genuine compile-time constants. Those are program constants, not system state.

See `engine/system/CLAUDE.md` § "Don't use function-local `static` for system state" for the full rationale.

## Preferred: member-on-`System<N>` via `registerSystem`

State lives as fields on the `System<N>` specialization itself; hooks are named member functions:

```cpp
template <> struct System<MY_NAME> {
    int counter_ = 0;             // params live as members

    void beginTick() { counter_ = 0; }
    void tick(C_Foo &foo) { counter_ += foo.x; }
    void endTick() { /* flush counter_ */ }

    static SystemId create() {
        return registerSystem<MY_NAME, C_Foo>("MyName");
    }
};
```

`registerSystem<N, Components...>(name, relationParams = {})`:

- `Components...` accepts the same `Exclude<...>` markers as `createSystem`.
- `tick(...)` is required — three accepted signatures (per-component, per-entity-id, per-archetype batch); the helper picks the right one by member detection.
- `beginTick()`, `endTick()`, `relationTick(RelComps&...)` are optional — wired only when defined on `System<N>`.
- The instance is `std::make_unique<System<N>>()`, owned by the system entity's params slot, freed when the system is destroyed.

Read the instance back via `getSystemParams<System<N>>(systemId)` (tests, diagnostics).

## Explicit: `Params` + `setSystemParams` (escape hatch)

The pre-`registerSystem` pattern. Same lifetime, same per-tick cost, more boilerplate. Reach for this when you need a custom params lifetime, multiple distinct params types per system, or you're maintaining an existing system already on this shape:

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

Notes (apply to both forms):

- The instance pointer is captured by value; it outlives the lambdas because the system entity owns the allocation.
- **Don't store raw references to params across frames** — if the system is recreated (e.g. via reload), the pointer is invalid.

## Three valid TICK function signatures

See `engine/system/CLAUDE.md` § "Three valid TICK function signatures".

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
