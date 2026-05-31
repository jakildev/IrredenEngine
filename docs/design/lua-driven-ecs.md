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
- [Retrospective — T-104 parity measurement](#retrospective--t-104-parity-measurement)
- [Navigation / steering authoring boundary (gaps G1–G4)](#navigation--steering-authoring-boundary-gaps-g1g4)

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
a single archview (`arch`), with `0..arch.length-1` row indices, and
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

-- Enums (#1403) -----------------------------------------------------
-- Closed enums defined in Lua, the Lua-native counterpart to the C++
-- registerEnum stopgap. Members map to 0-based ordinals in declaration
-- order; the returned handle is the enum table and is also stored at
-- IREnum.<Name>. No C++ lowering — a pure name->int table built the same
-- way in CODEGEN and EVAL (shared IRScript::detail::buildLuaEnumTable).
local DeviceType = IREnum.register("DeviceType", { "EFFECT", "SYNTH", "CONTROLLER" })
local kind = DeviceType.SYNTH                       -- 1; usable as an int32 default

-- Systems -----------------------------------------------------------
-- The shipped tick receives a single `arch` archview (NOT one column-table
-- argument per component); iterate rows by index and read/write each column
-- through `arch.<RegisteredName>`. The original column-list proposal
-- (`function(hp, tag) ... for i = 1, #hp`) was superseded by this archview
-- form before PR 3 shipped — see `engine/script/CLAUDE.md` §"Lua-defined
-- systems" for the full column-view surface (`:at`/`:setAt` for C++ columns,
-- `:getField`/`:setField`/`:getRow`/`:setRow` for Lua-defined columns).
local regenSystemId = IRSystem.registerSystem({
    name       = "Regen",
    components = { C_Hp, C_Tag },                   -- component handles; bare strings accepted but use handles for C++ types
    excludes   = { C_Dead },                        -- optional
    tick       = function(arch)                     -- single archview
        for i = 0, arch.length - 1 do               -- 0-based, arch.length rows
            local cur = arch.Hp:getField(i, "current")
            local max = arch.Hp:getField(i, "max")
            arch.Hp:setField(i, "current", math.min(max, cur + 1))
        end
    end,
})

-- Pipeline ----------------------------------------------------------
IRSystem.registerPipeline(IRTime.UPDATE, {
    IRSystem.systemId(SystemName.LIFETIME),
    regenSystemId,
    IRGameSystem.systemId(GameSystemName.GRID_BAKE),
})

-- Hot reload (EVAL mode only) ---------------------------------------
IRSystem.replaceSystemBody(regenSystemId, function(arch)
    -- new body, same archview shape
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

The Lua schema + tick body in `creations/demos/lua_perf_grid/main.lua`
mirror `C_PeriodicIdle` and `system_periodic_idle.hpp` for the 2-stage
SineEaseInOut wave perf_grid runs: same field set (flattened to flat
scalars: `vec3` → 3 floats, `std::vector<PeriodStage>` → two hardcoded
stage blocks), same per-tick arithmetic, same branch structure
(pause / cycle-wrap / stage-advance / map+ease+lerp + amplitude),
even bug-for-bug parity on `mapAngleToStageTValue`'s unused
`clampedAngle`. The `while`-loop stage advance is unrolled into a
single `if` (the DSL forbids `while` in CODEGEN bodies; correct for
2 stages — the loop body could only fire once).

| Demo                                  | Grid           | Wave-system per-tick    |
|---------------------------------------|----------------|-------------------------|
| `perf_grid` (C++) — `PeriodicIdle`    | 4³ = 64        | 0.004 ms (avg/call)     |
| `lua_perf_grid` (CODEGEN) — `LuaWaveTick` | 4³ = 64    | 0.003 ms (avg/call)     |
| `perf_grid` (C++) — `PeriodicIdle`    | 64³ = 262 144  | 2.082 ms (avg/call)     |
| `lua_perf_grid` (CODEGEN) — `LuaWaveTick` | 64³ = 262 144 | 0.948 ms (avg/call) |

**Ratio at grid_size=64: ~0.46×** — well under the 1.5× gate, and
this is the apples-to-apples comparison with logic-parity Lua.
Per-entity normalised at 64³: ~7.94 ns/entity (C++ `PeriodicIdle`) vs.
~3.62 ns/entity (Lua-CODEGEN `LuaWaveTick`).

Lua-CODEGEN beats C++ here because the *implementation* differs even
though the *logic* matches:

1. **Easing dispatch.** The engine's `kEasingFunctions` is a
   `std::unordered_map<IREasingFunctions, std::function<float(float)>>`.
   `kEasingFunctions.at(stage.easingFunction_)(mappedAngle)` is a
   hashmap lookup + indirect call through `std::function`. The
   codegen inlines `glm::sineEaseInOut(t) := -0.5 * (cos(pi*t) - 1)`
   directly — no hashmap, no `std::function`.
2. **Stage storage.** C++ uses `std::vector<PeriodStage> stages_` —
   heap indirection per access. Codegen unrolls the 2 stages into
   flat struct fields in one cache line.

Both are addressable on the C++ side (constexpr easing dispatch +
`std::array<PeriodStage, N>`) and would close the gap. The numbers
here are not a "Lua beats C++" claim — they're evidence that the
codegen path is structurally as fast as native, and that the C++
baseline has its own optimisation headroom.

**Frame-rate at 64³ (macOS Metal, MBP):** avg 15.62 ms / p50 15.23 ms /
p95 18.53 ms for the Lua demo — comfortably under the 16.67 ms (60 Hz)
budget. The C++ baseline at the same grid runs avg 17.82 ms / p50
18.09 ms / p95 22.93 ms (p50 slightly above mean — consistent with
bursty fast frames in the warmup window); difference is the
easing-dispatch + stage-storage cost noted above, not the runtime path.

> *Stage 3 grid_size=64 numbers measured on macOS Metal across two
> independent runs: T-109 first-pass (1.750 ms C++ / 0.958 ms Lua,
> ratio ~0.55×) and the T-104 testbed re-measurement after rebase
> onto T-109's CODEGEN runtime (2.082 ms C++ / 0.948 ms Lua,
> ratio ~0.46×, 300 frames each via `--auto-profile`). Both confirm
> the gate. The C++-side variance is host warmup/thermal state, not
> a regression. Linux fleet remains the canonical perf-comparison
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
- **PR #563** (T-104 testbed): held open through the T-105 → T-109
  chain; rebased onto master after T-109 merged. Re-measurement
  on the rebased state at 64³ (300 `--auto-profile` frames each,
  macOS Metal) confirms the gate: Lua-CODEGEN 0.948 ms / C++
  2.082 ms — ratio ~0.46×. Merged as the gate-passing artifact;
  EVAL-mode canonical-grid measurements remain a separate
  mechanical follow-up (see Stage 2 note).

## Navigation / steering authoring boundary (gaps G1–G4)

The parity gate (Retrospective, Stage 3) proved the Lua-driven ECS reaches
native speed for the **data-parallel scalar** entity shape — a per-entity
arithmetic kernel over flat columns. Engine #1400 reported the complementary
result: a **many-entity navigation / steering** stack of this class
(pathfinding, flow-field steering, spatial-neighbour collision over N agents)
*cannot be authored* in the Lua-driven ECS today, and identified four
separable gaps (G1–G4). This section records the decision per gap and the
authoring boundary they imply. It is the "design decision recorded per gap"
deliverable for #1400; the per-gap **Actionable as** lines are the scoped
follow-up engine tasks.

Each decision is one of two shapes:

- **Lua-expresses** — close the gap by extending the Lua / CODEGEN surface so
  the author writes the capability in Lua, lowering to native columns.
- **Engine-primitive** — the capability stays C++; Lua **consumes** it through
  a batched query or call. Reserved for cross-entity stateful structures.

### The boundary ruling

> **Per-entity data-parallel arithmetic is authored in Lua (CODEGEN);
> cross-entity stateful solvers stay C++ engine primitives that Lua consumes
> by batched query.** A Lua system never resolves cross-entity state by
> looping a per-candidate foreign accessor inside its tick.

This is not a new constraint — it is the union of three already-locked rules
read together:

- **Q2 (archetype-batched dispatch is the only system shape).** The cost model
  that makes parity reachable is "one `sol::function`/native body per
  archetype, flat-column per-entity work inside." A per-neighbour foreign read
  inside the loop is exactly the per-entity dispatch Q2 forbids.
- **The batched-foreign-entity rule** (`.claude/rules/cpp-ecs.md`
  §"Foreign-entity lookups", and its spatial corollary in
  [`lua-world-space-neighbour-query.md`](lua-world-space-neighbour-query.md)):
  the consumer iterates a **batched vector** the producer built at the frame
  boundary, not individual foreign lookups.
- **The parity retrospective.** Native speed was demonstrated for flat-SoA
  fields + one counted loop. Graph search, neighbour resolution, and
  incremental field propagation fit *neither* the flat-SoA storage model
  (ragged / shared state) *nor* the single-counted-loop control model — so
  they are outside the shape the gate validated, by construction.

The boundary is drawn **per capability**, not wholesale: Lua expresses the
embarrassingly-parallel arithmetic (steering integration, force accumulation
from already-queried neighbours, waypoint follow, field sampling); the engine
owns the cross-entity stateful structures (spatial index, pathfinder, field
grid). This is the "explicit *stays C++ as an engine-consumed primitive*
ruling" #1400 asked for, scoped to the solver shapes rather than to the whole
workload.

### G1 — structured / container component field types

Lua component fields are scalar-only today (`int32 / float / bool / string /
function / table`; `engine/script/include/irreden/script/lua_component_data.hpp`
`LuaFieldType`), and CODEGEN supports only `int32 / float / bool / string`.
The #1400 ask has two independent halves.

**G1a — `vec2 / vec3 / ivec3 / quat` scalar-packed fields → Lua-expresses.**
Today every vector field is hand-decomposed to N float fields — the
`lua_perf_grid` port literally flattens `vec3 → 3 floats` (Retrospective,
Stage 3). That is ergonomic friction, not an architectural boundary: a packed
vec/quat field lowers to the same native storage. The Q1 inference table
already *names* "Bound C++ usertype (`vec3.new(...)`)" as an inferable field
type, but neither the EVAL `LuaFieldColumn` variant nor the CODEGEN emitter
implements it — this gap closes that intent. Byte-identity with a C++ `vec3`
field (Q3) is preserved by storing the column as a real `IRMath::vec3` (EVAL)
/ a `vec3` struct member or three alphabetically-sorted float fields (CODEGEN).
Enum-typed fields are **not** part of this gap — `IREnum.register` already
yields an `int32`-valued member usable as a field default
(`engine/script/CLAUDE.md` §"Lua-defined enums").

- **Actionable as (engine task):** add `vec2 / vec3 / ivec3 / quat` to
  `LuaFieldType` + the EVAL `LuaFieldColumn` variant + the CODEGEN field-type
  set, with per-component access (`.x/.y/.z` or a swizzle) in the tick DSL.
  Bounded; no cross-entity state.

**G1b — per-entity dynamic array (variable-length list, e.g. a waypoint path)
→ engine-primitive / bounded, NOT an unbounded Lua column.** A true ragged
column is `std::vector<std::vector<T>>` — per-entity heap indirection, the
exact cache-hostile shape native-SoA storage exists to avoid, and it cannot
lower to CODEGEN's flat struct. Do **not** add an unbounded per-entity column.
Two honest paths cover the real need:

1. **Fixed-capacity inline arrays** (`waypoints = { type = "float[8]" }`) for
   bounded per-entity lists — these lower to flat columns and stay
   CODEGEN-native.
2. **Genuinely variable-length data stays engine-owned or EVAL-table.** A
   *path* is the output of the pathfinder primitive (G3), held in an engine
   handle, not re-stored as an unbounded Lua column; an author who insists on a
   variable-length Lua field uses the existing EVAL `table` type and pays the
   `sol::table` cost on purpose (Q1's opaque-table opt-in).

- **Actionable as (engine task):** a bounded inline-array field type
  (`"float[K]"` / `"int32[K]"`), CODEGEN-lowerable to a flat struct.
  Unbounded ragged columns stay out of scope.

### G2 — cross-entity / shared-index capability → engine-primitive (already decided)

**This gap is already resolved in design** by
[`lua-world-space-neighbour-query.md`](lua-world-space-neighbour-query.md)
(engine #1354): a once-per-frame-rebuilt **world-space spatial index**
(`BUILD_SPATIAL_INDEX` system + `C_SpatialIndex` singleton) queried as a batch
that returns `{ id, position }` records **inline**, so the consumer iterates a
vector and never loops per-neighbour foreign reads. That doc also corrects the
#1400 premise that Lua has no foreign-entity read surface — `getLuaField` /
`getLuaComponent` / `singleton` / `arch.entityAt(i)` already ship; the genuine
gap was only the *neighbour-find* half, which that design owns.

The remaining sub-gap #1400 raises under G2 — a Lua-side `beginTick` /
`endTick` hook so an author can build a **shared accumulator** (flow-field
grid, crowd-score grid) once per frame — is **deferred in favour of
engine-owned shared structures**, for the same reason as G3: a Lua `beginTick`
that mutates shared cross-archetype state is C++-only state that cannot lower
to CODEGEN, and it re-opens the cross-entity-ownership question the spatial
index answers cleanly. The spatial index is the first such primitive; a shared
**field-grid** primitive is its sibling, to be filed when a flow-field consumer
actually exists rather than speculatively.

- **Actionable as (engine task):** the spatial-index *implementation* task
  already blocked on #1354's doc (see its "Migration status"). A `C_FieldGrid`
  / `propagateField` primitive is a separate task filed when flow-fields land.
  Lua `beginTick`/`endTick` remains out of scope (consistent with
  `engine/script/CLAUDE.md` §"Lua-defined systems" → "No begin/end ticks yet").

### G3 — CODEGEN control-flow for iterative kernels → engine-primitive (solvers stay C++)

CODEGEN allows exactly one canonical counted loop — `for i = 0, arch.length - 1
do ... end` — and forbids `while` / `repeat` / `break` / generic-`for`
(`engine/script/CLAUDE.md` §"CODEGEN system bodies"). Dijkstra / A*-class
frontier loops and incremental field propagation cannot lower. These are
cross-entity stateful solvers — the architectural boundary the ruling draws.

**Do not add unbounded `while` to CODEGEN.** An unbounded loop emitted as
native C++ with no static bound is a footgun (a non-terminating tick body the
DSL cannot prove safe), and the solvers it would enable are exactly the
cross-entity state the engine should own. Instead the graph-search / field
solvers stay **C++ engine primitives Lua composes** — `Nav.findPath(start,
goal) -> path`, `propagateField(...)` — mirroring the G2 spatial-index decision.

An *optional, narrow* CODEGEN extension is compatible with the boundary: a
**bounded** counted loop with `break` (e.g. `for k = 0, maxIter - 1 do ... if
done then break end end`) is still statically bounded and CODEGEN-safe. It is
worth landing only if a real **bounded per-entity** iterative kernel (a fixed
relaxation step, not open-ended graph search) appears — filed separately, not
blocking, and explicitly *not* a license for unbounded `while`.

- **Actionable as (engine task):** a pathfinder / flow-field primitive surface
  (C++ core + Lua binding returning a batched result) when a consumer needs it.
  Optional secondary task: a bounded-`break` counted-loop CODEGEN extension,
  gated on a concrete bounded-iteration kernel.

### G4 — structural change in a Lua tick → Lua-expresses (deferred-command shim)

There is no add/remove-component mid-Lua-tick today (e.g. dropping a one-shot
order component after it is consumed). The engine already forbids the same
thing in C++ and routes it through the **deferred** ECS API
(`IREntity::deferredCreate` / `deferredSetComponent` / `deferredRemoveComponent`,
flushed by `flushStructuralChanges` at pipeline end; `.claude/rules/cpp-ecs.md`
§"Deferred entity operations"). `engine/script/CLAUDE.md` already states a Lua
tick "still needs to go through the deferred ECS API, just like a C++ system" —
the machinery exists; only the Lua binding is missing.

Decision: **bind the deferred ops to Lua** so a Lua tick queues structural
changes that flush safely at pipeline end. This respects the
no-mid-tick-structural-change invariant by construction and reuses existing
machinery — no new framework. Lower priority per #1400.

- **Actionable as (engine task):** Lua bindings for the deferred ECS ops
  (`IREntity.deferredSetComponent` / `deferredRemoveComponent` /
  `deferredCreate`), plus CODEGEN lowering of those calls to the same deferred
  C++ entry points.

### The documented authoring path

The #1400 acceptance criterion asks for "a documented path to author
many-entity navigation/steering in Lua-defined ECS (or an explicit *stays C++*
ruling)." With the decisions above, the path is a **hybrid**: engine primitives
own the cross-entity structures, Lua owns the per-entity arithmetic.

> **Note:** The path below assumes all four gaps have been closed; steps 2–5
> reference engine primitives (`Spatial.queryRadius`, `Nav.findPath`) that are
> future tasks — see the per-gap Actionable-as lines above for implementation
> status.

1. **Index** — the engine rebuilds the world-space spatial index once per frame
   (`BUILD_SPATIAL_INDEX`, ordered before every consumer). [G2]
2. **Steer** — a Lua CODEGEN system calls `Spatial.queryRadius(pos, r)` → a
   batched `{ id, pos }` vector, and computes separation / alignment / cohesion
   (or collision push-out) as native per-entity arithmetic over `vec3` fields.
   No per-neighbour foreign read. [G1a + G2]
3. **Path-find** — pathfinding is an engine primitive: `Nav.findPath(start,
   goal)` returns a path; the Lua system *follows* it (waypoint advance is a
   bounded per-entity kernel). The path lives in an engine handle or a bounded
   inline-array field, not an unbounded Lua column. [G1b + G3]
4. **Flow-field** — flow fields are an engine field-grid primitive (sibling to
   the spatial index); a Lua system samples the field per entity (native read)
   and integrates velocity. [G2 + G3]
5. **One-shot orders** — a "move-to" order consumed once is dropped via the
   deferred-structural shim. [G4]

Net: the steering integration, force accumulation, waypoint follow, and field
sampling are authored in Lua at native speed; the spatial index, pathfinder,
and field grid are engine-consumed primitives. A workload that is *only* the
graph-search / field-propagation solver (no per-entity arithmetic worth
expressing) legitimately **stays C++** — that is the explicit primitive ruling,
and it is the legitimate-C++ reason a downstream consumer's review gate asks
for.

### Decision summary

| Gap | Capability | Decision | Actionable as |
|-----|------------|----------|---------------|
| G1a | `vec2/vec3/ivec3/quat` packed fields | **Lua-expresses** | add vec/quat to `LuaFieldType` + `LuaFieldColumn` + CODEGEN field set |
| G1b | per-entity variable-length list | **Engine/bounded** | bounded inline-array field (`"float[K]"`); unbounded out of scope |
| G2  | spatial index / neighbour query | **Engine-primitive** (decided) | spatial-index impl (blocked on #1354 doc); field-grid sibling later |
| G2′ | Lua `beginTick`/`endTick` shared accumulator | **Deferred** | covered by engine primitives; not exposed |
| G3  | graph-search / iterative control-flow | **Engine-primitive** | pathfinder/field primitive; optional bounded-`break` CODEGEN ext |
| G4  | structural change mid-tick | **Lua-expresses** | bind deferred ECS ops to Lua + CODEGEN lowering |

These follow-ups are filed as engine tasks from this doc once the human
approves the recorded decisions; per fleet convention this design PR does not
self-expand the queue.
