# engine/entity/ — IRECS archetype store

Archetype nodes store entities with the same component set in parallel dense
columns. Adding or removing a component moves the entity to another node.
The public creation-facing boundary is
[`ir_entity.hpp`](include/irreden/ir_entity.hpp); internal code may use
`EntityManager` directly when it deliberately needs eager behavior.

## Structural mutation and iteration

`forEachComponent` supports per-component, entity-plus-component, and
per-archetype column callbacks. Systems use the same signature dispatch; see
[`engine/system/CLAUDE.md`](../system/CLAUDE.md). Never structurally mutate an
archetype while iterating it. Use the deferred APIs and let
`flushStructuralChanges()` drain them at a safe frame boundary.

`destroyEntity` has two intentionally different meanings:

| Spelling | Timing | Thread |
|---|---|---|
| `IREntity::destroyEntity(id)` | marks the entity; it stays queryable until `destroyMarkedEntities()` | main or worker |
| `IREntity::getEntityManager().destroyEntity(id)` | tears the entity down immediately | main only |

Use the manager method only when the entity must be gone before the next
statement. An unqualified manager member call is eager.

### Worker staging

Deferred operations append to the caller's per-worker slot. Entity creation
allocates an id immediately, but a worker-created entity is not placed in an
archetype until the next flush; do not read or mutate its components before
then. Worker count is fixed after `World` constructs the entity manager.
Flushes drain legacy main-thread buffers first and then staging slots in
deterministic worker-id order.

Worker-callable structural changes must route through staging: worker-side
`createEntity`, deferred component mutation, and deferred destruction do so.
Eager graph mutation, batch creation, direct manager destruction,
`flushStructuralChanges()`, and `destroyMarkedEntities()` are main-thread-only.
The debug assertions are not release-build synchronization.
The general system-side rule lives in
[the ECS rules](../../.claude/rules/cpp-ecs.md#deferred-entity-operations-during-tick).

## Component registration

Typed and runtime-defined components share one `ComponentId` space and the
same archetype storage and teardown hooks. Dynamic insertion calls the
registered storage implementation's `appendDefaultRow()`; implementations
that cannot default-construct a row must reject that path, and callers must
supply an explicit value through the typed API.

## Record lookup

- `findRecord(id)` is the non-inserting probe for ids that may be dead or not
  yet placed. A non-null record can still have a null node while creation or a
  batch insert is pending.
- `getRecord(id)` asserts that the record exists and is placed. Use it when a
  missing id is a caller bug.
- Never probe `m_entityIndex` with `operator[]`; a miss would create a bogus
  record. Mask metadata bits before indexing by entity id.

Deferred drains use set semantics and skip ids already gone. Direct eager
destruction of a dead id is a caller error and asserts before hooks run.

## Pre-destroy hooks

`registerPreDestroyHook` callbacks run in registration order while the dying
entity and its peers remain queryable. Keep each hook at most O(world), and
put component-local cleanup in `onDestroy()` instead.

During a callback:

- Do not unregister hooks or mutate the dying entity's archetype.
- Peer mutation and deferred peer destruction are allowed; eager reentrant
  destruction should be avoided.
- Do not lazy-create a singleton during `destroyAllEntities()`, directly or
  through another service. Use `singletonEntityOrNull<T>()` or
  `singletonOrNull<T>()`.
- A hook replacing an existing teardown path preserves all of that path's
  invariants, not only its entity-id cleanup. The owning subsystem must keep
  derived state consistent; see
  [system-owned invariants](../../.claude/rules/cpp-ecs.md#system-owned-invariants-encapsulate-dont-delegate-to-callers).

## Singleton components

Singleton components are normal ECS rows cached by `ComponentId`. They
participate in archetype iteration and moves, so iteration order is not a
singleton lookup contract. Use the typed singleton accessors for the specific
row.

- `singleton<T>()` lazy-creates with `T{}`; a non-default-constructible type
  needs a feature-owned factory that supplies its initial value.
- Every lookup validates the cached id. `destroyAllEntities()` clears the
  cache; `resetGameplay()` preserves it and its values.
- Use singleton components for world-scoped ECS data. External device
  resources remain owned by their manager.
- The API does not name singleton entities; callers may assign a diagnostic
  name when useful.

World snapshots persist singleton values separately from regular archetype
rows and map saved singleton ids to their live ids. Snapshot format and load
ordering are owned by [`engine/world/CLAUDE.md`](../world/CLAUDE.md).

## Snapshot restore surface

The loader-only `EntityManager` restore APIs are frame-boundary,
main-thread-only operations. They materialize empty archetypes, insert exact
saved entity ids before columns are filled in matching row order, expose the
save walker's preserve predicates, and only advance the monotonic id
watermark. Normal ECS code does not use this surface.

## Scene reset

`IREntity::resetGameplay()` eagerly destroys gameplay entities while
preserving singleton entities, `C_Persistent` entities, and component-type
backing entities. Run it only at a frame boundary, then replace pipelines,
then create the next scene. `destroyAllEntities()` is for world teardown and
tests, not scene transitions.

After reset:

- Registered pre-destroy hooks sweep only the fields they own. Reacquire any
  surviving entity id that has no such hook.
- Dead named-entity entries are pruned; surviving entities retain names.
- System-owned state and Lua globals persist. Per-scene state belongs in
  components that the reset destroys.
- Assert repeatability by live counts and resource counts, never by ids;
  entity ids do not recycle.
- Relation entities are not preserved. Query the surviving relationship by
  its child archetype rather than testing the old relation entity id.

## Cross-module contracts and pitfalls

- `createEntity(...)` automatically attaches `C_LocalTransform` and
  `C_WorldTransform`; the facade avoids duplicate rows when either is passed
  explicitly. Transform propagation is documented in
  [`engine/prefabs/irreden/common/CLAUDE.md`](../prefabs/irreden/common/CLAUDE.md).
- Relations are component-like archetype entries, so `setParent` can move an
  entity to another node.
- Never retain a component pointer or archetype row index across structural
  mutation; retain the `EntityId` and look it up again.
- `removeComponentsSimple()` performs one archetype mutation per entity; use
  deferred removal and one flush for bulk work.
- Component registration is lazy. Register explicitly before creation only
  when stable registration order is required.
- Manager access is valid only during `World`'s lifetime; the canonical
  lifecycle and ownership rules are in
  [the global-state rules](../../.claude/rules/cpp-globals.md#sanctioned-patterns).

Validators are indexed in [`docs/agents/VALIDATION.md`](../../docs/agents/VALIDATION.md);
the relevant documentation gates are `lint_instruction_size.py` and
`lint_comment_refs.py`.
