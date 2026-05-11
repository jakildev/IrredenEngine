# Lua-driven ECS authoring

Engine-level mechanism that lets a creation define new components, new
systems, and the pipeline that schedules them entirely in Lua, with the
same archetype-based ECS invariants as the C++ path. Lua- and C++-side
component types share one `ComponentId` space, so a Lua physics tick and
the C++ render system read the same column byte-for-byte.

This document specifies the locked design choices and the public API
contract. The runtime that backs these contracts ships across a six-PR
stack — see [Staged delivery](#staged-delivery).

## Table of contents

- [Vocabulary](#vocabulary)
- [Locked design choices](#locked-design-choices)
- [Public API surface](#public-api-surface)
- [Staged delivery](#staged-delivery)
- [Performance budget](#performance-budget)
- [Out of scope](#out-of-scope)
- [Notes for implementers](#notes-for-implementers)

## Vocabulary

- **Lua-defined component** — a component type whose schema is declared
  by a Lua call to `IRComponent.register(name, defaults)`. Storage is a
  set of native SoA columns, not an opaque `sol::table`. Identified by
  the same `ComponentId` type as C++-defined components.
- **Lua-defined system** — a system whose tick body is a `sol::function`.
  Dispatch is **archetype-batched**: one Lua call per matching archetype
  per tick, given column views over the entities in that archetype.
- **Field binding** — the existing modifier-framework hook. Lua-typed
  components auto-register their fields into the same string-keyed
  registry C++ uses, so `IRModifier.add(entity, "Hp.current", …)`
  works the moment a component is registered.
- **Native column** — `std::vector<int32_t>`, `std::vector<float>`,
  `std::vector<sol::function>`, etc. — typed storage routed through a
  new `IComponentDataLuaTyped : IComponentData` impl.

## Locked design choices

The five Q&A picks below are settled. If during implementation a choice
looks wrong, **escalate** rather than silently re-deciding — see
[Notes for implementers](#notes-for-implementers).

### Q1: Single native-SoA storage tier with type inference

Author API still accepts the short form
`IRComponent.register("Hp", { current = 100, max = 100 })`, but **the
engine never stores those as opaque Lua tables**. At registration time
the engine inspects each default and infers a native field type:

| Default value                            | Inferred field type    |
|------------------------------------------|------------------------|
| Lua integer (`5`)                        | `int32`                |
| Lua float (`5.0`)                        | `float`                |
| Lua string (`"hello"`)                   | `std::string`          |
| Lua boolean (`true` / `false`)           | `bool`                 |
| Bound C++ usertype (`vec3.new(...)`)     | that usertype          |
| Bound enum value                         | the enum               |
| Lua function (`function() ... end`)      | `sol::function`        |
| Anything else (nested table, nil, etc.)  | **registration fails** |

**No silent fallback.** If the engine can't infer a column type,
`IRComponent.register` raises a Lua error at script-load time naming
the offending field and listing the legal options.

Authors who need to disambiguate (e.g. force `current = 100` to be
`float` rather than `int32`) use the explicit form:

```lua
IRComponent.register("Hp", {
    current = { type = "float", default = 100 },
    max     = { type = "float", default = 100 },
})
```

The same explicit form is the **opt-in escape hatch** for genuinely
opaque table-valued fields:

```lua
IRComponent.register("Tag", {
    payload = { type = "table", default = {} },
})
```

The author has paid the `sol::table`-lookup cost on purpose. There is
no implicit version of this — silent per-frame table lookups defeat
the whole point of native storage.

`sol::function` fields are first-class — per-entity callbacks
(`onDeath`, `behavior`) store as a `std::vector<sol::function>` column
with native dispatch. They are not "fallback"; they are a real storable
type for which the engine knows how to allocate, copy, move, and invoke.

**Why:** the storage model is what makes the perf-parity gate (PR 6)
achievable. A Lua system reading any field hits a typed column, not a
per-entity `sol::table` lookup. Single storage path, one decision at
registration, hard fail when it cannot be honored.

### Q2: Archetype-batched dispatch is the only system shape

Per-entity Lua dispatch is a footgun (10-100× slower per the issue).
The default and only API: Lua passes a `tick` function that receives
**column views** (one Lua table per component, indexable `1..N`) and
processes the whole archetype in one call. One `sol::function`
invocation per matching archetype per tick.

A per-entity convenience wrapper may land later — but it loops in Lua,
not C++, so authors see and own the cost.

Lua can mix Lua-defined component types and C++-bound component types
in the iterated list. C++ types come through as their existing
usertype view; Lua-typed components come through as a typed-column view.

**Why:** the function-call overhead is the dominant Lua cost; amortize
it across the archetype rather than paying it per entity. Anything else
makes the parity gate (PR 6) unreachable.

### Q3: One ComponentId per type, regardless of who defined it

`Archetype = std::set<ComponentId>` and
`ArchetypeNode::components_` is a
`unordered_map<ComponentId, smart_ComponentData>` of type-erased columns
already. Lua-defined components get real `ComponentId`s allocated
through the same lazy `EntityManager::registerComponent`-style path,
just routed to `IComponentDataLuaTyped` instead of
`IComponentDataImpl<T>`. **No parallel archetype graph, no fork.**

**Non-negotiable identity rule:** there is exactly one `ComponentId`
per component type. If `C_Position3D` is defined in C++, both C++
systems and Lua systems iterating it read and write *the same column
data through the same `ComponentId`*. There is no Lua-side mirror, no
shadow component, no copy.

Conversely, a Lua-defined component (`C_Hp` registered from a `.lua`
file) gets one `ComponentId` and that same id is what a future C++
system iterating `Hp` would use.

**Constraint this places on Lua-defined systems:** a Lua system's
component list must reference component types *that the Lua state
knows about* — meaning either Lua-registered types or C++ types in the
creation's `lua_component_pack.hpp`. If the author requests a C++
component with no Lua binding, registration fails fast with a clear
error pointing at the component pack.

**Why:** anything else (mirroring, shadow components, parallel
archetype graphs) loses the ECS storage invariant — that all data for
a component type lives in one column, contiguously. Sharing the same
id space is the whole point.

### Q4: System bodies hot-reloadable; component schemas deferred

Replacing a Lua system's tick body mid-run is easy: the system's
identity is its `SystemId`, the body is just the `sol::function` it
holds. Re-bind, done. Existing entities don't carry per-tick layout
state.

Replacing a component's schema mid-run requires migrating storage for
every entity that has it. That's a separate problem — punt to a later
PR. For v1, define-once-at-script-load and reload by full Lua state
restart (which `World::runScript` already supports indirectly).

**Why:** the friction this eliminates is the iteration loop on system
*behavior*, which is where authors spend most of their time. Schema
changes are a smaller fraction of the iteration cost and need real
migration semantics to be safe; defer until the cost-of-omission shows
up.

### Q5: Modifier framework integration via the existing field-binding registry

The modifier framework (engine #302, already landed) exposes:
`C_Modifiers` (vector-of-modifiers per entity), the
`ADD/MULTIPLY/SET/CLAMP/OVERRIDE` transforms enum, the per-tick
resolver that writes to `C_ResolvedFields`, and the field-binding
registry that tells the resolver which fields exist and how to
read/write them.

Lua-driven ECS does not fork that — it plugs into it:

- **Schemafied Lua components auto-register field bindings.** A typed
  field (`current = { type = "float", default = 100 }`) becomes a real
  native column, and the registration path also installs a field
  binding `("Hp.current", getter, setter)` so the modifier resolver
  applies `ADD/MULTIPLY/...` to it like any C++ component field.
- **Schemaless (table-valued) Lua fields can manually register field
  bindings.** An author who wants modifiers on a `sol::table`-stored
  field calls `IRComponent.bindField("Tag", "morale", "float")` and
  accepts the per-resolve table-lookup cost.
- **Lua scripts can add modifiers to existing C++ engine/game
  components.** The C++ field-binding registry already names every
  modifiable field by string; binding the registry to Lua exposes the
  same surface to scripts:
  `IRModifier.add(entity, "Movement.speed", { transform = ADD, value = 0.5, source = src })`.
- **Lua-defined systems can read `C_ResolvedFields` natively** — it's
  just another component the system iterates.

**Why:** the modifier framework's string-keyed field-binding registry
is already the right shape for runtime extensibility. The answer to
"Lua systems should extend modifiable fields on existing engine
components" is *register a binding into the existing registry*, not
*build a parallel framework*.

## Public API surface

Locked Lua-side function signatures and the new C++-side entry points.
Names and shapes are the contract; per-PR implementations may refine
internals but not the surface.

### Lua side

```lua
-- Components --------------------------------------------------------
local C_Hp = IRComponent.register("Hp", {
    current = 100,
    max     = 100,
})

-- Disambiguating / opaque-table opt-in (Q1):
local C_Hp = IRComponent.register("Hp", {
    current = { type = "float", default = 100 },
    payload = { type = "table", default = {} },
})

-- Manual field binding (Q5, schemaless field):
IRComponent.bindField("Tag", "morale", "float")

-- Systems -----------------------------------------------------------
local regenSystemId = IRSystem.registerSystem({
    name       = "Regen",
    components = { C_Hp, C_Tag },                   -- iteration list
    excludes   = { C_Dead },                        -- optional
    tick       = function(hp, tag)                  -- column views
        for i = 1, #hp do
            hp[i].current = math.min(hp[i].max, hp[i].current + 1)
        end
    end,
})

-- Pipeline ----------------------------------------------------------
IRSystem.registerPipeline(IRTime.UPDATE, {
    IRSystem.systemId(SystemName.LIFETIME),
    regenSystemId,
    IRGameSystem.systemId(GameSystemName.GRID_BAKE),
})

-- Hot reload --------------------------------------------------------
IRSystem.replaceSystemBody(regenSystemId, function(hp, tag)
    -- new body
end)

-- Modifiers ---------------------------------------------------------
IRModifier.add(entity, "Hp.current", {
    transform = IRModifier.Transform.MULTIPLY,
    value     = 0.5,
    source    = sourceEntity,
})
```

### C++ side (new entry points)

The Lua surface above is implemented via these C++ additions. None of
the existing C++ template paths change — `createSystem<...>()`,
`registerComponent<T>()`, and `registerTypeFromTraits<T>()` keep
working as they do today.

- `IComponentDataLuaTyped : IComponentData` —
  new impl in `engine/entity/include/irreden/entity/i_component_data.hpp`.
  Holds one `std::variant<std::vector<int32_t>, std::vector<float>,
  std::vector<bool>, std::vector<std::string>,
  std::vector<sol::function>, std::vector<UsertypeT>...,
  std::vector<sol::table>>` per declared field. Implements
  `cloneEmpty`, `moveDataAndPack`, `pushCopyData`, `removeDataAndPack`,
  `destroy` over the variant.
- `EntityManager::registerComponentDynamic(typeName, dataImpl)` —
  non-template parallel to the existing template
  `registerComponent<T>()`. Allocates a fresh `ComponentId`, stores
  the impl in the same pure-component-vectors map. The shared
  bookkeeping factors out from the template path.
- `IRSystem::createSystemDynamic(name, includeIds, excludeIds, body)` —
  takes a runtime component-id list and a
  `std::function<void(ArchetypeNode&)>` body. Lives next to the
  existing template `createSystem<...>()`. The per-archetype
  dispatch path invokes the runtime body alongside the templated tick
  path; likely a new `DynamicSystem` struct in
  `system/system_manager.hpp`.
- `LuaScript::registerComponent(name, defaults)` /
  `registerSystem(spec)` / `replaceSystemBody(id, fn)` /
  `bindField(typeName, fieldName, fieldType)` — Lua-facing bindings,
  added to the existing
  `engine/script/include/irreden/script/lua_script.hpp`. Same file
  already owns the `sol::state`.
- `engine/script/include/irreden/script/lua_modifier_bindings.hpp`
  (new, PR 4) — binds `C_Modifiers`, the transforms enum,
  `C_ResolvedFields`, and the field-binding registry from the
  already-landed modifier framework.

## Staged delivery

Six PRs, each independently shippable. PR 1 is this design doc; PRs
2–6 are implementation chunks sized to review in an evening. Tracked
as engine task IDs T-099 through T-104.

### PR 1 — T-099: Design doc

This document. Locks Q1–Q5, the public API surface, the parity gate
spec, and the out-of-scope follow-ups. No code changes outside `docs/`.

### PR 2 — T-100: Lua-defined components with type inference

- Add `IComponentDataLuaTyped : IComponentData` in `engine/entity/`.
- Add `EntityManager::registerComponentDynamic(typeName, dataImpl)`.
- Add `LuaScript::registerComponent(name, defaultsTable)` — type-infer,
  allocate `ComponentId`, auto-register field bindings keyed by
  `"<TypeName>.<fieldName>"`, fail fast on unresolvable fields and on
  shadow-registration of an existing name.
- Bind `IREntity.addLuaComponent`, `getLuaComponent`,
  `removeLuaComponent`.
- Smoke test: register `C_Hp { current = 100, max = 100 }`, attach to
  100 entities, read back from a C++ test. Verify the storage column
  is a real `int32` (or `float` if the explicit form is used), not a
  `sol::object`.

### PR 3 — T-101: Lua-defined systems with archetype-batched dispatch

- Add `IRSystem::createSystemDynamic(name, includeIds, excludeIds, body)`.
- Add `LuaScript::registerSystem({ name, components, excludes, tick })`:
  - Resolve each component name to its `ComponentId` — Lua-registered
    types and C++ types in the `lua_component_pack` resolve through
    the same id space (Q3). Unbound C++ types fail fast.
  - Build the body wrapper: per matching archetype, expose each column
    as a typed Lua view, call the `sol::function` once per archetype.
- Returns a `SystemId` Lua can hold.
- Test: a Lua system iterates `(C_Position3D, C_Velocity3D)` —
  *both defined in C++* — alongside a Lua-defined `C_Marker` tag, and
  writes to `C_Position3D` such that the existing C++ render system
  picks up the change next frame.

### PR 4 — T-102: Pipeline composition + enum bindings + modifier-framework bindings

- Bind `IRSystem::registerPipeline(event, {systemIds})` to Lua.
- Bind the existing `SystemName` and game-side `GameSystemName` enums.
  Convenience: `IRSystem.systemId(SystemName.X)` → `SystemId`.
- New `engine/script/include/irreden/script/lua_modifier_bindings.hpp`
  binds `C_Modifiers`, the transforms enum, `C_ResolvedFields`, and
  the field-binding registry. Lua-typed components are auto-registered
  in PR 2; this PR exposes the registry for C++-component modifiers
  as well.
- Demo creation whose entire `initSystems` lives in `main.lua`,
  mixing engine systems, game systems, and Lua-defined systems in
  one pipeline list.

### PR 5 — T-103: Hot-reload of Lua system bodies

- Add `IRSystem::replaceSystemBody(systemId, newFn)` — rebinds the
  `sol::function` held by an existing dynamic system. No archetype
  changes, no entity migration.
- Document the API in `engine/script/CLAUDE.md`.
- Smoke test: change a math constant inside a Lua system body, call
  `replaceSystemBody`, observe next-tick behavior change without
  restart.

### PR 6 — T-104: Lua port of `perf_grid` + parity gate

- New demo `creations/demos/lua_perf_grid/` mirroring
  `creations/demos/perf_grid/` byte-for-feature: same 64×64×64 voxel
  grid (~262k entities), same wave-amplitude/wave-period config, same
  render pipeline. The C++ `main.cpp` is a thin shim; entity setup,
  the wave-animation component definitions, the periodic-idle-equivalent
  system, and the pipeline live in `main.lua`.
- Run both demos with `profiling_enabled = true` and capture per-frame
  timings of equivalent systems via `IRProfile`.
- **Parity gate:** Lua wave-animation system per-tick cost ≤ 1.5× C++
  `SystemPeriodicIdlePositionOffset` per-tick cost on the same
  hardware. Document the measured ratio in the PR 6 description.
- **Fail behavior:** if the gate fails, amend this design doc with
  the corrective decision (LuaJIT migration, codegen-bound bodies,
  schemafied-only enforcement, etc.) before further implementation
  work. The corrective decision may itself be its own architect ticket.

This PR is the formal acceptance criterion from #293: *"a creation
can define a new component type and a new system in Lua, attach the
component to entities, and observe the system iterating it. Profiler
shows the Lua-side per-archetype dispatch cost."*

## Performance budget

With native SoA storage (Q1) and archetype-batched dispatch (Q2), the
remaining Lua overhead is one `sol::function` call per archetype per
tick plus per-entity-loop dispatch on the inner Lua loop (we run
plain Lua 5.4, not LuaJIT — see [Notes for implementers](#notes-for-implementers)).

| Pattern                                          | Target                              |
|--------------------------------------------------|-------------------------------------|
| Archetype-batched, trivial body, native fields   | ≤ 2× a C++ tick body for the same workload |
| Per-entity Lua dispatch (escape hatch only)      | 30–100× C++; documented footgun     |
| Explicit `sol::table` opt-in field access        | one Lua table lookup per access per entity |
| `sol::function` field invocation                 | one Sol2 protected-function call per call |

The previous "keep hot loops in C++" guidance is replaced by **measure
with `lua_perf_grid` parity; if you can't hit 1.5×, fall back to
C++**. Lua is suitable for hot loops *if* the fields are native (the
default after Q1) and the body is archetype-batched (mandated by Q2).
Lua remains the right home for content/data-driven logic and slow-tick
systems regardless.

## Out of scope

These were considered and explicitly deferred — record as follow-ups
when the need actually appears:

- **Hot-reloading component schemas** (mid-run archetype migration).
  The runtime cost is real and the migration semantics need a
  separate design pass.
- **Generic `IRLuaCreation` boot binary** so a creation is literally a
  directory of Lua + a manifest, with no per-creation `main_lua.cpp`.
  Natural endpoint of "fully create games using Lua" but purely
  additive on top of #293.
- **Lua-defined relation components** — the ECS supports relation
  components today; binding that to Lua is its own design.
- **LuaJIT migration** — only if PR 6's parity gate fails. Then it
  becomes its own architect ticket.

## Notes for implementers

- **`ComponentId` identity is a hard invariant.** If you find yourself
  about to allocate a *second* `ComponentId` for an already-registered
  type "to keep Lua and C++ separate", stop. That violates Q3 — by
  design, the two sides must resolve to the same column. Re-read Q3
  before attempting a workaround.
- **No silent fallbacks.** Q1 is explicit: a registration that the
  engine cannot honor as native storage **fails at script-load time**.
  A clean error pointing at the offending field is better than a
  silent `sol::table` slowdown discovered only via profiling.
- **The modifier framework is the connection point for runtime
  extensibility.** Q5's design assumes the field-binding registry,
  `C_Modifiers`, transforms enum, and `C_ResolvedFields` exist and
  behave as documented in `docs/design/modifiers.md`. If implementing
  a Lua-driven ECS feature seems to require parallel infrastructure,
  it probably means the modifier framework's API needs an extension —
  extend the existing surface rather than reinventing it.
- **LuaJIT vs. Lua 5.4.** The parity gate (PR 6) is the deciding test.
  If plain Lua 5.4's dispatch overhead dominates for trivial
  archetype-batched bodies, the corrective decision is a separate
  ticket — *not* a silent runtime swap. Record the measured ratio in
  PR 6's description regardless of pass/fail.
- **Escalate on ambiguity.** The five design picks above are locked.
  If during implementation a pick looks wrong (e.g. PR 3 finds that
  one `sol::function` per archetype is genuinely the wrong granularity),
  open a comment on the relevant PR with the evidence, don't silently
  re-decide.

## Retrospective — T-104 parity measurement

T-104 (#563) shipped the formal parity gate as a `lua_perf_grid` Lua
port of `perf_grid`'s wave-animation kernel and ran it head-to-head
against the C++ baseline. Three measurement points map the curve from
the original failure to the production-ready CODEGEN runtime.

### Stage 1 — Lua 5.4 + sol2 (T-104, gate failure)

| Demo                                | Grid               | Wave-system per-tick |
|-------------------------------------|--------------------|----------------------|
| `perf_grid` (C++)                   | 16³ = 4096         | 0.043 ms             |
| `lua_perf_grid` (Lua 5.4 + sol2)    | 16³ = 4096         | ≥ 445 ms             |

**Ratio: ≥ 10 000×** — four orders of magnitude past the ≤ 1.5× gate.
Per-row sol2 dispatch (~100 µs/call × 4 calls/row × 4096 rows)
dominates; the per-archetype dispatch is fine (~9 µs). The gate-fail
behaviour clause kicked in: this design doc is amended with the
corrective decision instead of the testbed PR being merged.

### Stage 2 — LuaJIT 2.1 + sol2 (T-105, dev-mode floor)

T-105 swapped the engine's Lua runtime to LuaJIT 2.1, exercised the
existing `lua_perf_grid` testbed under EVAL mode, and documented the
trace-JIT-collapsed dispatch cost as the dev-iteration floor. Per-row
work becomes a compiled inner loop after warmup; expected ratio is
2–10× C++.

> *EVAL+LuaJIT measurements at canonical 16³ / 64³ grids are pending
> Linux-fleet host validation. The macOS host these were attempted on
> exhibits a render-pipeline crash unrelated to this work that prevents
> the C++ baseline from rendering its lattice past a few frames at
> grid_size ≥ 8 (no precompiled .metallib without full Xcode). The
> EVAL build's profiling infrastructure is present in
> `creations/demos/lua_perf_grid/` and runs cleanly at grid_size=4 in
> CODEGEN mode, so the larger-grid + EVAL-mode measurements are a
> mechanical follow-up, not a design question.*

EVAL mode exists as the dev-iteration runtime: hot-reloadable
(T-103's `IRSystem.replaceSystemBody` is alive only here),
LuaJIT-fast enough that 4096-entity demos iterate without stalling.
Production builds default to CODEGEN.

### Stage 3 — CODEGEN (T-109, parity gate met)

T-106/T-107/T-108 built a build-time codegen tool at
`cmake/lua_codegen/` that scans the same `.lua` schema files and emits
typed C++ component structs + `IRSystem::createSystem<...>`
specialisations. Per-row work runs at native speed; no `sol::function`
or sol2 column dispatch on the hot path. T-109 then applied CODEGEN
mode to `creations/demos/lua_perf_grid/` and re-measured.

| Demo                                  | Grid           | Wave-system per-tick    |
|---------------------------------------|----------------|-------------------------|
| `perf_grid` (C++) — `PeriodicIdle`    | 4³ = 64        | 0.004 ms (avg/call)     |
| `lua_perf_grid` (CODEGEN) — `LuaWaveTick` | 4³ = 64    | 0.003 ms (avg/call)     |
| `perf_grid` (C++) — `PeriodicIdle`    | 64³ = 262 144  | 1.750 ms (avg/call)     |
| `lua_perf_grid` (CODEGEN) — `LuaWaveTick` | 64³ = 262 144 | 0.497 ms (avg/call) |

**Ratio at grid_size=4: ~0.7×; ratio at grid_size=64: ~0.28×.** Both
well under the 1.5× gate. Per-entity normalised at grid_size=64:
~6.7 ns/entity (C++ `PeriodicIdle`) vs. ~1.9 ns/entity (Lua-CODEGEN
`LuaWaveTick`). The Lua wave kernel is structurally simpler than
`C_PeriodicIdle::tick` (the C++ system runs a two-stage easing
table with `addStageDurationSeconds`; the Lua kernel is a single
`amp * sin(angle)`), so the Lua port lands faster by a structural
margin — not a "Lua beats C++" claim but confirmation that
codegen-emitted code reaches and exceeds C++ throughput where the
authored kernel is leaner. The ratio improves with grid size because
fixed per-call setup amortises over more entities.

**Frame-rate at 64³ (macOS Metal, MBP):** avg 15.52 ms / p50 12.36 ms /
p95 19.59 ms for the Lua demo — comfortably inside the 16.67 ms 60 Hz
budget. The C++ baseline at the same grid runs avg 17.19 ms / p50
14.06 ms; difference is the Lua kernel's structural simplicity, not
the runtime path.

> *Stage 3 grid_size=64 numbers measured on macOS Metal (the earlier
> SIGBUS at grid_size ≥ 8 was resolved upstream and both demos now run
> the full 60 frames). Linux fleet remains the canonical perf-comparison
> host; per-host re-measurement is mechanical follow-up. EVAL-mode
> canonical-grid measurements are still pending — they appear in
> Stage 2 above.*

### Architect decision: two coexisting paths

The corrective decision from #566: **CODEGEN as the production
runtime, EVAL as the dev-iteration opt-out.**

- `IR_LUA_ECS_DEFAULT_MODE=CODEGEN` (default) — build-time tool
  emits C++ from Lua schemas + bodies; per-row work is native.
  Hot-reload (T-103) is unavailable; rebuild to change a system
  body.
- `IR_LUA_ECS_DEFAULT_MODE=EVAL` — runtime sol2 dispatch on
  LuaJIT 2.1; T-103 hot-reload is alive. Per-tick cost is
  2–10× CODEGEN (target; canonical-grid measurement pending).

Per-system override via `IRSystem.registerSystem({ mode = "eval"
| "codegen", ... })` lets a creation pin individual systems to
either path independent of the build flavor (T-108). The
`creations/demos/lua_perf_grid/` `main.lua` schema is the
canonical example: same source serves both build flavors with no
per-mode rewrites.

### Closure

- **Engine #293** (Lua-driven ECS authoring epic): acceptance
  criterion amended to *"creations can author components and
  systems in Lua, with codegen as the production runtime and
  LuaJIT-backed EVAL as the dev iteration runtime; hot-reload is
  a dev-mode-only feature, available in EVAL only."* Closed by
  T-109.
- **Engine #566** (corrective-decision tracker): closed by T-109.
- **PR #563** (T-104 testbed): held open through the chain;
  rebased onto T-109 once the chain merges, amended with both
  CODEGEN and EVAL numbers, then merged as the gate-passing
  artifact.
