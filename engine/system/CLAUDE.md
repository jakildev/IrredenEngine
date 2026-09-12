# engine/system/ — pipeline scheduler

Systems are entities whose tick handlers run against matching archetypes. The
built-in pipelines execute `INPUT` → `UPDATE` → `RENDER`; system order inside a
singleton-group pipeline is declaration order.

Cross-cutting ECS rules live in
[`CLAUDE-BASELINE.md`](../../docs/agents/CLAUDE-BASELINE.md). System-state rules
and their live-deviation register live in
[`cpp-systems.md`](../../.claude/rules/cpp-systems.md).

## Build and test

```bash
fleet-build --target IrredenEngineTest
fleet-run IrredenEngineTest --gtest_filter='*System*:*Pipeline*'
```

The validation index is [`VALIDATION.md`](../../docs/agents/VALIDATION.md).

## Registration and tick forms

Prefer `registerSystem<N, Components...>` for named C++ systems. It owns one
`System<N>` instance in the system entity's params slot and detects the required
`tick` plus optional `beginTick`, `endTick`, and `relationTick` members.
`Components...` may contain `Exclude<...>` and access-policy tags.

### Three valid TICK function signatures

`createSystem<Components...>` and `registerSystem<N, Components...>` accept:

```cpp
void tick(C_A &a, const C_B &b);                        // per component
void tick(EntityId id, C_A &a, const C_B &b);           // entity id + components
void tick(const Archetype &, std::vector<EntityId> &,   // whole archetype
          std::vector<C_A> &, std::vector<C_B> &);
```

Use the component form by default, the id form only when the id is required,
and the archetype form for genuine batch work. `const` component types declare
reads to `SystemAccess`; mutable component types declare writes.

### Begin, end, and relation ticks

- `beginTick()` and `endTick()` take no arguments and run once per dispatched
  system, even when no entities match.
- `relationTick(RelComps &...)` requires matching `RelationParams<RelComps...>`
  and runs once per parent relation group.
- Structural edits from any tick use the deferred entity API; pipeline group
  boundaries flush them.

### Runtime-typed systems and one-shot queries

`createSystemDynamic` accepts resolved include/exclude archetypes and invokes a
`void(ArchetypeNode *)` body once per matching node. The body owns row iteration.
Dynamic systems have no `SystemName` registry entry and cannot use
`Concurrency::PARALLEL_FOR` because they have no ranged row binder.

`executeQuery<Components...>` runs the same three tick forms immediately without
registering a system. `executeQueryDynamic` is its whole-node core. Queries are
serial and main-thread-only, do not flush structural changes, and silently do
nothing when no archetype matches. Cadence never gates direct queries.

### Hot reload and lookup

`replaceSystemBody` replaces only a whole-node system's non-empty body. It keeps
the id, filters, params, cadence, concurrency, and pipeline membership unchanged;
row-iterating systems cannot use it.

`findSystem(SystemName)` returns the enum-registered system id or
`kNullSystemId`. Dynamic systems are absent. Treat `0` as a valid id and use only
`kNullSystemId` as the missing sentinel. Registering one name to different ids is
a debug assertion failure.

## Per-system parameters

State belongs to the system entity and has the system's lifetime:

- Preferred: fields on `System<N>`, registered with
  `registerSystem<N, Components...>` and retrieved with
  `getSystemParams<System<N>>(id)`.
- Escape hatch: allocate a `Params` object, capture its raw pointer in the
  `createSystem` callbacks, then transfer ownership with
  `setSystemParams(id, std::move(owner))`.

Use the escape hatch only for an existing system already shaped that way, custom
ownership, or multiple independently owned params objects. Never retain a raw
params pointer across system recreation.

### Don't use function-local `static` for system state

Mutable or system-owned state must use one of the params forms above. Only true
program constants and reset-on-entry `static thread_local` scratch storage are
exceptions. The canonical rule and known violations are in
[`cpp-systems.md`](../../.claude/rules/cpp-systems.md).

## Pipelines and groups

`registerPipeline(event, systems)` replaces the event's pipeline and gives every
system its own group. `registerPipelineGroups(event, groups)` also replaces it;
groups run sequentially while members of a multi-system group co-execute. Every
group boundary flushes deferred structural changes.

`validateAllPipelineGroups()` runs after registration and rejects a multi-system
group when:

- any member is `MAIN_THREAD`;
- any member is itself `PARALLEL_FOR`;
- two members write the same component; or
- one member writes a component another reads.

`Spawns` and `Destroys` do not conflict by themselves: worker-private deferred
mutation buffers are drained on the main thread at the group boundary. Declare
all extra access with `AlsoReads<...>` / `AlsoWrites<...>` so validation sees it.

Multi-system groups skip `TickObserver` callbacks. A system that needs observer
brackets belongs in a singleton group. With no job manager, a multi-system group
falls back to serial declaration-order dispatch.

### Live pipeline composition

`appendToPipeline`, `insertIntoPipelineBefore`, and
`insertIntoPipelineAfter` add a system as its own singleton group. The insert
forms require a live anchor; adding the same system twice is a debug assertion
failure. Clear the system's old event pipeline before joining it to a different
event, because its cadence state is bound to one event clock.

`clearPipeline(event)` is equivalent to registering an empty pipeline. Pair a
scene-transition clear with the entity-side gameplay reset at a frame boundary;
removing a system from a pipeline makes it inert but does not destroy it.

## Concurrency and access policy

`Concurrency` is `SERIAL` by default. `MAIN_THREAD` documents a required main
thread. `PARALLEL_FOR` chunks each matched node by `kGrainSize`; the main thread
resolves component columns before workers receive range closures. Archetype
iteration plus begin/end hooks remain serial.

A `PARALLEL_FOR` registration is invalid when it is batch-form, relation-form,
`MainThread`-tagged, or entity-id-form without an explicit `ParallelSafe` tag.
The tag is an audit claim: it does not make manager lookups thread-safe.

### IR_ASSERT_MAIN_THREAD

Use `IR_ASSERT_MAIN_THREAD()` at manager-entry APIs that mutate or traverse
non-thread-safe global managers and may be reached from a worker tick. It is a
debug-only assertion and treats a missing job manager as main-thread execution.

## Per-system cadence

Cadence is a pipeline scheduling throttle. `cadence == 1` runs every event tick;
`0` normalizes to `1`. An offset staggers the first run and is normalized into
the cadence range. Declare `kCadence` / `kCadenceOffset` on `System<N>`, pass the
trailing values to `createSystem`, or use the runtime setters.

An off-cadence system skips its entire dispatch: observers, begin/end hooks,
archetype lookup, and row iteration. Group-boundary structural flushes still run.
`executeSystem` and `executeQuery` are ungated.

Use `getAccumulatedTicks(id)` to scale work over skipped event ticks.
`accumulatedDeltaTime(id)` is only for fixed-step `UPDATE`; variable-rate phases
use the tick count directly. Changing cadence re-phases from the last run;
changing offset and re-registering a pipeline re-phase from that system's event
clock.

## Gotchas

- Never call component lookup APIs per entity; use the dense tick arguments or
  the alternatives in the baseline ECS rule.
- A system that changes archetypes directly while iterating can skip or revisit
  rows. Use deferred mutations.
- Every enum-named system needs a `SystemName` entry; missing entries usually
  surface as linker failures.
- Relation callbacks run only when registration supplies `RelationParams`.
- Pipeline registration replaces; use the append/insert APIs for composition.
