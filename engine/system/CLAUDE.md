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

When a system needs persistent state beyond components (a GPU buffer
handle, an accumulator, a snapshot taken at `beginTick` and read by the
per-entity tick), there are two ways to attach it. Both have the same
runtime lifetime — params owned by the system entity, freed when the
system is destroyed — and the same per-tick cost (one pointer capture,
no per-frame lookup).

### Preferred: member-on-`System<N>` via `registerSystem`

The system's state lives as **member fields on the `System<N>`
specialization itself**, with `tick` / `beginTick` / `endTick` /
`relationTick` as named member functions. `registerSystem<N, Cs...>`
allocates the `System<N>` instance, captures its pointer into the
per-tick lambdas, and hands ownership to the system entity's params
slot. No nested `Params` struct, no `setSystemParams` call.

```cpp
template <> struct System<HITBOX_MOUSE_TEST_GUI> {
    // Member variables = params. No nested Params struct.
    vec2 mouseGuiTrixel_ = vec2(0.0f);

    // Per-entity tick — normal member function, no lambda, no captures.
    void tick(C_HitBox2DGui &hitbox, const C_GuiPosition &guiPos) {
        const vec2 lo = vec2(guiPos.pos_);
        const vec2 hi = vec2(guiPos.pos_ + hitbox.size_);
        hitbox.hovered_ =
            mouseGuiTrixel_.x >= lo.x && mouseGuiTrixel_.x < hi.x &&
            mouseGuiTrixel_.y >= lo.y && mouseGuiTrixel_.y < hi.y;
    }

    void beginTick() {
        // ... compute frame-scoped state ...
        mouseGuiTrixel_ = mouseFb / fbRes * guiSize;
    }

    static SystemId create() {
        return registerSystem<HITBOX_MOUSE_TEST_GUI,
                              C_HitBox2DGui,
                              C_GuiPosition>("HitBoxMouseTestGui");
    }
};
```

`registerSystem<N, Components...>(name, relationParams = {})`:

- `Components...` may include `Exclude<...>` markers, same as
  `createSystem`.
- `tick(...)` is required. Three accepted signatures, mirroring
  `createSystem`'s three TICK forms (per-component, per-entity-id,
  per-archetype batch). The helper picks the right one by member
  detection.
- `beginTick()`, `endTick()` are optional; presence is detected via
  concept and they're wired only when defined.
- `relationTick(RelComps&...)` is optional; pair it with a
  `RelationParams<RelComps...>` argument so the helper can match
  the member's signature.
- The instance is `std::make_unique<System<N>>()` — held in the
  same `m_systemParams` slot the explicit pattern uses, freed
  identically when the system is destroyed.

`getSystemParams<System<N>>(systemId)` returns the instance pointer
when you need to reach into the system from outside (tests,
diagnostics).

### Explicit: `Params` + `setSystemParams` (escape hatch)

The pre-`registerSystem` pattern. Allocate a separate `Params` struct,
build the per-tick lambdas with a captured raw pointer, hand the
allocation to the system entity via `setSystemParams` **after**
`createSystem` returns:

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

Same lifetime, same per-tick cost, more boilerplate. Reach for this
form when:

- The params type needs a custom lifetime (allocated elsewhere,
  re-seated by an external owner, etc.).
- A single system juggles multiple distinct params types and you want
  them as separate allocations.
- You're maintaining an existing system already on this pattern and
  the migration isn't worth the diff right now.

Both forms coexist; new systems should default to `registerSystem`.

```cpp
// Either path: read the params back from outside the tick.
auto* params = getSystemParams<MyParams>(systemId);            // explicit
auto* sys    = getSystemParams<System<MY_NAME>>(systemId);     // member-on-System<N>
```

**Don't store raw references to params across frames** — if the
system is recreated (e.g. via reload), the pointer is invalid.

### Don't use function-local `static` for system state

Function-local `static` for system-owned state is an anti-pattern.
Use one of the two patterns above instead.

**Why it's wrong:**
- Hidden state — not visible to ECS inspectors or system-walking tools.
- Lifetime mismatch — persists for program lifetime, doesn't free when the
  system entity is destroyed.
- Single-instance assumption — all instances of `System<X>` share the same
  statics; future multi-instance use silently cross-talks.
- Conflicts with the ECS "everything on an entity" philosophy.

**Why the perf argument doesn't hold:** both supported patterns
(`registerSystem` member-on-`System<N>` and the explicit `Params`
form) have the same per-tick access cost as `static`. The pointer is
captured by value into the lambdas at `create()` time — the lookup
happens once, not per tick.

**Exception:** truly invariant data — `constexpr` integer constants,
named-resource pointers fetched once at engine init that never change — is
fine as `static`. Those are program constants, not system state. The rule
applies to *mutable* or *system-owned* state.

See `.fleet/status/system-static-deviations.md` (queue-manager-owned;
feature PRs do not edit) for the current list of files still using
function-local `static` for system state.

## Pipelines

Three built-in pipelines execute in this order each frame: **INPUT → UPDATE → RENDER**.

Within each pipeline, systems execute in the order listed in `registerPipeline`. Common orderings:

**UPDATE:** velocity/acceleration → goto/easing → global position → voxel children → lifetime

**INPUT:** key/mouse → gamepad → hover detect

**RENDER:** camera pan → velocity render → voxel-to-trixel stages → shapes/text → trixel compositing → framebuffer → debug overlay → screen

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

