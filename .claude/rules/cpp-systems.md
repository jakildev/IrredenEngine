---
paths:
  - "engine/prefabs/**/system_*.{hpp,cpp}"
  - "engine/system/**"
  - "creations/**/system_*.{hpp,cpp}"
---

> Sweep with `fleet-rules-sweep`, never `rg`/`Grep` rooted at `creations/`
> — see [`README.md`](README.md).

# System state lives on System<N> or in SystemParams, never function-local static

Rule:

> **Never** use function-local `static` for *mutable* or *system-owned*
> state inside a system tick or its `create()` function. Use the
> member-on-`System<N>` form (preferred) or the explicit `SystemParams` form.

Allowed:

- `static constexpr` / `static const` **value** constants.
- `static thread_local` **scratch buffers reset on entry** — cleared or
  reassigned at the top of the function, never read before written; only
  heap capacity persists. A buffer that carries meaning across calls is
  state, not scratch.

Not allowed despite the spelling: `static const T *p = nullptr;` is a
*mutable pointer* to const data — reassigning it is hidden system state.

Rationale: `engine/system/CLAUDE.md` §"Don't use function-local `static` for system state".

## Preferred: member-on-`System<N>` via `registerSystem`

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

`registerSystem<N, Components...>(name, relationParams = {})`: `Components...`
accepts the same `Exclude<...>` markers as `createSystem`; `tick(...)` is
required (per-component, per-entity-id, or per-archetype batch, picked by
member detection); `beginTick()`, `endTick()`, `relationTick(RelComps&...)`
are optional. The instance is owned by the system entity's params slot; read
it back with `getSystemParams<System<N>>(systemId)`.

## Explicit: `Params` + `setSystemParams` (escape hatch)

Same lifetime and per-tick cost, more boilerplate: allocate the params with
`std::make_unique`, capture the raw pointer by value in the `createSystem`
lambdas, then `setSystemParams(id, std::move(owner))`. Use it for a custom
params lifetime, multiple params types per system, or a system already on
this shape. In both forms, never store a raw params reference across frames
— a recreated system invalidates it.

## Three valid TICK function signatures

See `engine/system/CLAUDE.md` § "Three valid TICK function signatures".

## beginTick / endTick contract

- `functionBeginTick` / `functionEndTick` fire once per pipeline execution,
  before / after the per-entity ticks. Signature `void()` — no `Archetype&`,
  no component params.
- Both run even when zero entities match; check `ids.size()` yourself.
- `functionRelationTick` fires per parent under `RelationParams<...>` and
  takes an `EntityRecord`.

## Detection

Two sweeps — a single `\bstatic\b` pattern is drowned by `static SystemId
create()` / `static constexpr`, and the indent-anchored form misses the
`static const T *p` class inside namespace-scope `inline` accessors. The
pattern is Python `re`, not POSIX (`[[:alnum:]]` degrades to a 0-match false
clean). Positive controls: `system_gravity.hpp:17` and
`system_modifier_resolve_global.hpp:35` must appear in the output while they
remain in the register below — if either is absent, the pattern is broken.

```
fleet-rules-sweep --pattern '^\s{8,}static\s+(?!constexpr\b|const\b|void\b|auto\b)' \
  --glob 'engine/prefabs/**/system_*.{hpp,cpp}' --glob 'engine/system/**' \
  --glob 'creations/**/system_*.{hpp,cpp}' .
fleet-rules-sweep --pattern 'static\s+const\s+.*\*\s*\w+\s*=' \
  --glob 'engine/prefabs/**/system_*.{hpp,cpp}' --glob 'creations/**/system_*.{hpp,cpp}' .
```

## Live deviations

Don't add new violations; migrate when already touching one of these files.
Paths relative to `engine/prefabs/irreden/`.

| Site | Shape |
|---|---|
| `common/systems/system_modifier_resolve_global.hpp:35,40,45` | 3 `static const T *p = nullptr;` frame caches reassigned from `beginTick` (mutable despite the `const`). Keeps `MODIFIER_RESOLVE_GLOBAL` pinned `SERIAL`. |
| `input/systems/system_hitbox_mouse_test.hpp:26-30` | 5 statics declared in `create()` and captured by the tick lambda. |
| `render/systems/system_debug_overlay.hpp:87-88` | 2 static vertex vectors inside the tick body. |
| `update/systems/system_action_animation.hpp:24` | `static std::unordered_map<...> clipCache` in `create()`. |
| `update/systems/system_gravity.hpp:17` | `static C_Gravity3D instance{};` — keeps `GRAVITY_3D` pinned `SERIAL`; migrate with `MODIFIER_RESOLVE_GLOBAL`. |
| `update/systems/system_rhythmic_launch.hpp:29` | `static std::unordered_map<...> platformCache`. |

Each moves to the member-on-`System<N>` form (preferred) or `SystemParams`.

Not deviations (allowed scratch, listed so a sweep doesn't re-flag them):
`render/systems/system_shapes_to_trixel.hpp:459` and
`render/systems/system_voxel_to_trixel.hpp:58` — `static thread_local`
buffers reset on entry.
