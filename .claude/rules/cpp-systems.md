---
paths:
  - "engine/prefabs/**/system_*.{hpp,cpp}"
  - "engine/system/**"
  - "creations/**/system_*.{hpp,cpp}"
---

> **Sweeping for violations?** `paths:` is an injection scope, not a search
> root. `rg`/`Grep` rooted at `creations/` reads a **false clean** (#2739) —
> run detectors through `fleet-rules-sweep`. See [`README.md`](README.md).

# System state lives on System<N> or in SystemParams, never function-local static

Rule:

> **Never** use function-local `static` for *mutable* or *system-owned* state inside a system tick or its `create()` function. Use the member-on-`System<N>` form (preferred) or the explicit `SystemParams` form.

Allowed:

- `static constexpr` / `static const` **value** constants. Those are program
  constants, not system state.
- `static thread_local` **scratch buffers reset on entry** — cleared or
  reassigned at the top of the function and never read before being written, so
  no value survives the call. Only the heap capacity persists, which is the
  point (no per-frame allocation in a render hot path), and `thread_local`
  rules out the cross-instance cross-talk this ban exists to prevent. If the
  buffer carries meaning across calls it is state, not scratch — migrate it.

**Not allowed despite the spelling:** `static const T *p = nullptr;` is a
*mutable pointer* to const data. The pointee is const; the variable is not, and
reassigning it is exactly the hidden system state this rule bans. Three such
caches shipped under this misreading — see Live deviations.

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

The register is this list — it lives here, next to the rule it qualifies, not
in a separate tracking file (see #2733).

Don't add new violations. Migrate when you're already touching one of these
files for other reasons; don't delay other work to migrate aggressively.

**Measured 2026-07-31** over the `paths:` globs above (134 tracked files) —
16 sites in 7 files. Re-measure when you edit this list; a stamp that drifts
from the tree is what made the previous register useless. Paths below are
relative to `engine/prefabs/irreden/`.

| Site | Shape |
|---|---|
| `common/systems/system_modifier_resolve_global.hpp:35,40,45` | 3 `static const T *p = nullptr;` frame caches, handed out by non-const reference so `beginTick` can reassign them. Spelled `const`, but mutable — see the Allowed clause. Keeps `MODIFIER_RESOLVE_GLOBAL` pinned `SERIAL`. |
| `input/systems/system_entity_hover_detect.hpp:132` | `static EntityEventHandlers instance;` — tracked separately in **#2582** (migrating to a singleton component). |
| `input/systems/system_entity_hover_detect.hpp:140` | `static IREntity::EntityId previousHoveredEntity`. |
| `input/systems/system_entity_hover_detect.hpp:182` | `static int logCounter` — log throttle. |
| `input/systems/system_hitbox_mouse_test.hpp:26-30` | 5 statics declared in `create()` and captured by the tick lambda (`s_mouseCanvas`, `s_cameraIso`, `s_cameraZoom`, `s_fbResHalf`, `s_cardinalIndex`). |
| `render/systems/system_debug_overlay.hpp:87-88` | 2 static vertex vectors declared **inside the tick body**. |
| `update/systems/system_action_animation.hpp:24` | `static std::unordered_map<...> clipCache` in `create()`. |
| `update/systems/system_gravity.hpp:17` | `static C_Gravity3D instance{};` — keeps `GRAVITY_3D` pinned `SERIAL`; migrate with `MODIFIER_RESOLVE_GLOBAL`. |
| `update/systems/system_rhythmic_launch.hpp:29` | `static std::unordered_map<...> platformCache`. |

Each should move to the member-on-`System<N>` form (preferred) or `SystemParams`.

**Not deviations** (allowed per the Allowed clause, listed so the next sweep
doesn't re-flag them): `render/systems/system_shapes_to_trixel.hpp:446` and
`render/systems/system_voxel_to_trixel.hpp:58` — `static thread_local` scratch
buffers, both reset on entry.

**Retired entries** — fixed, do not re-add: `system_entity_canvas_to_framebuffer.hpp`
(migrated to an `instances_` member by #1520) and `system_animation_color.hpp:25-26`
(the clip caches no longer exist there; the surviving one is
`system_action_animation.hpp:24`, listed above).
