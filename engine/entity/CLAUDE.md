# engine/entity/ — IRECS archetype store

Archetype-based ECS: groups entities sharing the same set of component types
into dense arrays ("archetype nodes") and iterates them columnwise. Changing
an entity's archetype (add/remove component) moves it between nodes.

## Entry point

`engine/entity/include/irreden/ir_entity.hpp` — exposes `IREntity::`
free functions: `getEntityManager()`, `createEntity(...)`,
`createEntityBatch(...)`, `createEntityBatchWithFunctions(...)`,
`forEachComponent(...)`, `setParent(...)`, etc.

## Key types

- **`EntityId`** — `uint64_t` alias. High bits hold metadata flags
  (`IR_PURE_ENTITY_BIT`, `kEntityFlagIsRelation`). Mask with
  `IR_ENTITY_ID_BITS` to get the raw id.
- **`ComponentId`** — distinct from `EntityId` at the type level; every
  component type is registered once via `registerComponent<C>()` (lazy,
  triggered by first use).
- **`Archetype`** — `std::set<ComponentId>` describing an entity's set of
  components.
- **`ArchetypeNode`** — the dense SoA storage for one archetype: one vector
  of `EntityId`, plus one parallel vector per component type.
- **`ArchetypeGraph`** — holds all nodes and the add/remove edges between
  them. `findCreateArchetypeNode()` walks or extends the graph as entities
  mutate.
- **`Relation`** enum — `NONE`, `CHILD_OF`, `PARENT_TO`, `SIBLING_OF`.
  Relations are registered as pseudo-components via `registerRelation()`.

## Component registration: template vs dynamic

`registerComponent<T>()` is the C++ path: a `ComponentId` is allocated
the first time the type is referenced, backed by an
`IComponentDataImpl<T>` storing one `std::vector<T>` per archetype.

`registerComponentDynamic(typeName, smart_ComponentData)` is the
runtime path: caller supplies a fully-constructed `IComponentData`
impl and a user-visible name. Used by Lua-defined components (see
`engine/script/CLAUDE.md` "Lua-defined components") which back their
columns with the script-layer `IComponentDataLuaTyped`. Both paths
share `m_pureComponentTypes` / `m_pureComponentVectors` so
`ComponentId`s are drawn from one space; archetype storage,
move/pack/remove, and pre-destroy hooks all work uniformly.

Dynamic add: `addComponentDynamic(entity, componentId)` adds a
runtime-registered component using the impl's `appendDefaultRow()`.
Default impl returns `false` — non-Lua impls reject this path so a
caller is forced to use `setComponent<T>(entity, value)` with an
explicit value (matters for components with `= delete`d default
ctors, e.g. `C_CanvasAOTexture`). Dynamic readers go through
`getComponentDataAndRow(entity, componentId)` which returns the
type-erased `IComponentData*` and row index; the caller casts to
the concrete impl.

## Iteration API

`forEachComponent(lambda)` iterates all archetype nodes that contain a
component type. The lambda's signature determines the iteration shape
(detected at compile time via `std::is_invocable_v`):

```cpp
// per-component (most common)
forEachComponent<C_Velocity3D>([](C_Velocity3D& v) { ... });

// per-component with entity id
forEachComponent<C_Velocity3D>([](EntityId id, C_Velocity3D& v) { ... });

// per-archetype (batch / SIMD-friendly)
forEachComponent<C_Velocity3D>(
    [](const Archetype& arch, std::vector<EntityId>& ids,
       std::vector<C_Velocity3D>& vels) { ... });
```

The same signature dispatch is what `IRSystem::createSystem<>` uses
internally — see `engine/system/CLAUDE.md` for the system-side view.

## Deferred structural changes

Modifying an entity's archetype *during* iteration is unsafe. Use the
deferred API:

- `removeComponentDeferred(id, C)` — queue a removal.
- `setComponentDeferred(id, C{...})` — queue an add/set.
- `flushStructuralChanges()` — apply queued changes at a safe point (the
  frame boundary in most pipelines).

### Per-worker buffers + thread safety (T-225)

The deferred API is callable from worker threads inside a
`PARALLEL_FOR` system body. The mechanism is per-worker staging:

- The `EntityManager` holds `m_workerStaging`, a vector indexed by
  `IRJob::workerId()` (slot `0` = main thread, slots `1..N` =
  IRJob worker threads). Workers append to their own slot; no
  lock is needed on the producer side.
- The vector is sized at `World` construction time, immediately
  after `JobManager` is constructed, via
  `EntityManager::resizeWorkerStaging(workerCount + 1)`. Worker
  count must not change after that point — if the pool resized
  mid-frame, queued worker writes would land in the wrong slot.
- `createEntity` from a worker is also safe: the EntityId is
  allocated atomically via `m_nextEntityId.fetch_add(1)` and
  returned to the caller immediately. The actual archetype-node
  insertion is staged into the worker's slot and runs on the
  main thread at the next `flushStructuralChanges`. Callers may
  hold the ID but must not call `getComponent` / `setComponent`
  on it until after flush.
- `markEntityForDeletion` from a worker queues into the worker's
  slot; `destroyMarkedEntities` (run from `World::update` on the
  main thread) drains the legacy main vector first, then each
  worker slot in order.
- `flushStructuralChanges` runs on the main thread (asserted)
  and drains the legacy vectors first, then each per-worker slot
  in `workerId` order. The drain order is deterministic so
  `--auto-screenshot` reproducibility holds across sessions —
  the same set of worker spawns/destroys produces the same
  archetype-node row layout.

#### Not callable from workers

The following APIs must only be called from the **main thread**. Calling them
from a `PARALLEL_FOR` worker body bypasses per-worker staging and produces a
data race in release builds (the `isMainThreadForDeferred()` assert catches
misuse only in debug builds):

**Eager mutation APIs** (directly modify the archetype graph in place):
- `setComponent<C>(id, value)` — use `setComponentDeferred` instead
- `removeComponent<C>(id)` — use `removeComponentDeferred` instead
- `removeComponentById(id, componentId)`
- `destroyEntity(id)` — use `markEntityForDeletion` instead
- `setComponents(id, ...)` (multi-component overloads)
- `insertNewComponent<C>(id, value)`
- `createEntityBatch(...)` / `createEntitiesBatch(...)` — batch paths do not
  route through per-worker staging

**Flush APIs** (drain staging buffers; assert main-thread at entry):
- `flushStructuralChanges()`
- `destroyMarkedEntities()`

A worker-callable `Spawns` or `Destroys` system in a `PARALLEL_FOR` group must
use only the deferred API. The validator lifts `MUTATOR_IN_PARALLEL_GROUP`
(T-225) for systems that follow this contract; it cannot enforce the contract
at the call site — use the deferred variants deliberately.

The atomic ID counter replaced the old recycle pool. IDs are no
longer reused; the `IR_ENTITY_ID_BITS` (25-bit) space gives ~33M
entities per session, sufficient for current workloads. Long-running
sessions that approach the cap should switch to a tiered allocator —
not yet needed.

## Pre-destroy hooks

`EntityManager::registerPreDestroyHook(callback)` registers a
`std::function<void(EntityId)>` that fires inside `destroyEntity`
**before** the entity's components are torn down. The hook receives the
dying `EntityId` while the entity (and every peer) is still fully
queryable, so the callback can iterate other entities and strip
references to the dying id — typical use is the modifier framework's
auto-sweep of source-attributed modifiers off live target entities.

Returns a `PreDestroyHookId` token. Pass to `unregisterPreDestroyHook`
to remove. Hooks fire in registration order.

Constraints:

- A hook MUST NOT unregister any hook (itself or others) while a
  `destroyEntity` is in progress. Doing so would silently skip a
  sibling hook; an `IR_ASSERT` fires in debug builds to catch this.
  Defer any unregistration until `destroyEntity` returns.
- A hook MUST NOT mutate the about-to-be-destroyed entity's archetype
  (no `setComponent` / `removeComponent` on that id). Mutating peer
  entities (other ids) is fine.
- A hook MAY call `markEntityForDeletion` on peer entities, but SHOULD
  NOT call `destroyEntity` reentrantly during the hook.
- A hook MUST NOT call the lazy-create singleton accessor
  (`IREntity::singletonEntity<T>` / `IREntity::singleton<T>`) during
  `destroyAllEntities`. The bulk teardown iterates a snapshot of
  `m_entityIndex` and clears `m_singletonEntityByComponent` only at
  the end; a mid-loop lazy-create would mint a fresh singleton entity
  that the snapshot can't see, leaving an unreachable "ghost" entity
  in the index after the cache is cleared (cache empty, but
  `forEachComponent<T>` still iterates the row — no way back to it
  via `singletonEntity<T>`). Use the no-create variants
  `singletonEntityOrNull<T>` / `singletonOrNull<T>` instead, or check
  `entityExists` before reading; the same applies in any pre-destroy
  hook that might run during a bulk reset.
- The cost is O(hooks × destructions); each hook should be O(world)
  at worst. Don't register hooks that run an expensive search per
  destroy.

The framework-level use case is wiring engine-wide invariants (modifier
sweeps, owned-resource cleanup) that would otherwise force every caller
to remember a manual cleanup step before `destroyEntity`. Per-component
cleanup belongs in the component's `onDestroy()` member, not in a hook
— see `engine/prefabs/CLAUDE.md` "Documented exceptions".

## Singleton components

A "singleton component" is a component for which exactly one record
exists per world — framework-level globals, per-world settings, scratch
state. The canonical example is the modifier framework's
`C_GlobalModifiers` (one `"modifierGlobals"` entity per world); the
sim-clock substrate (#200) and future per-world game-rules holders will
adopt the same shape.

`IREntity::singleton<T>()` is the public API for this pattern. Lazy-init
on first call (creates an entity with default-constructed `T` and caches
the id by `ComponentId`); subsequent calls return the same reference at
the cost of one hash-map lookup. Singleton entities are normal ECS
entities and participate in archetype iteration — a `forEachComponent<T>`
that matches `T` will see the singleton row.

```cpp
// Typed C++ entry points (engine/entity/include/irreden/ir_entity.hpp):
template <typename C> IREntity::EntityId singletonEntity();        // lazy-create
template <typename C> IREntity::EntityId singletonEntityOrNull();  // no-create
template <typename C> C& singleton();                              // lazy-create + ref
template <typename C> C* singletonOrNull();                        // no-create + ptr
```

Lua: `IREntity.singleton(componentDef) -> LuaEntity`. Works for
codegen'd-as-C++ components and runtime-registered Lua-defined
components — both share the same `ComponentId` space and the same
cache. Pass the returned `LuaEntity` to the standard
`IREntity.getLuaComponent` / `IREntity.setLuaField` accessors.

```lua
local C_GameRules = IRComponent.register("GameRules", { score = 0 })
local entity = IREntity.singleton(C_GameRules)         -- lazy-creates first time
IREntity.setLuaField(entity, C_GameRules, scoreIdx, 1) -- normal field accessor
```

### Conventions

- **Default-constructible.** The typed `singleton<T>()` path calls
  `createEntity(T{})`. If `T` cannot be default-constructed, expose a
  feature-specific factory wrapping `singletonEntity<T>` so callers can
  seed the row with explicit values.
- **Lazy validation.** The cache is keyed by `ComponentId` and validated
  against `entityExists` on every lookup. If a singleton entity is
  destroyed externally (manual `destroyEntity` call, end-of-world
  `destroyAllEntities` reset), the next access lazy-recreates (typed
  path) or returns `kNullEntity` / `nullptr` (or-null path).
- **`destroyAllEntities` resets the cache.** Tests that tear down and
  rebuild the world between cases get a fresh singleton on the first
  post-reset access.
- **Naming is optional.** The API does NOT auto-name the entity; callers
  who want diagnostic visibility can `setName(singletonEntity<T>(),
  "myThing")`. The modifier framework names its globals
  `"modifierGlobals"` for the existing tooling that scans by name.
- **Use the API for what it's for.** Singleton-shaped *components* — yes.
  Things that should live on a manager (graphics device handles,
  RenderManager fields) — no; manager fields are still the right
  shape for device-level state. Rule of thumb: if the data is naturally
  an ECS column (composable, iterable, save/load-friendly), use a
  singleton component; if it's a pointer to an external resource that
  the manager already owns, leave it on the manager.

### Archetype implications

A singleton entity is a normal entity in the archetype graph. Adding or
removing components on it moves it between archetype nodes the same way
any other entity does. Iteration order across nodes is implementation-
defined, so don't assume the singleton appears first or last in a
`forEachComponent` walk — query by `singletonEntity<T>()` when you need
the specific row.

### Save/load implications

The world snapshot (`engine/world/world_snapshot.hpp`, persist P2 #2213)
does **not** ride a singleton through the archetype chunk — it excludes
singleton entities from the `ARCH` walk and persists each by value in the
`SNGL` chunk instead, restoring it onto the live singleton via
`getOrCreateSingleton<C>()` and overwriting the row. So on load the cache
rebuilds against the (possibly freshly lazy-created) live entity, and the
snapshot's `LoadResult.singletonAliases_` maps each saved singleton id to
its live id (identity in the same-session `destroyAllEntities`-then-load
case; a fresh id cross-session). The standard load contract still runs a
teardown first, which clears the cache.

## World-snapshot restore surface (persist P2, #2213)

`EntityManager` exposes a small surface the snapshot **loader** needs and
the normal ECS flow doesn't:

- `findCreateArchetypeNode(type)` — public wrapper over the archetype
  graph's node creation (the loader must materialize a restored archetype
  no live entity currently occupies; `findArchetypeNode` only *finds*).
- `restoreEntitiesBatch(node, span<EntityId>)` — insert a batch of entities
  with their **exact saved ids** into `node` (records + `entities_` +
  `length_` + live count). Columns are filled afterward by the caller's
  per-column readers, in the same entity order — the caller asserts the
  end-of-node column/`length_` sync, mirroring the eager insert path.
- `singletonEntityCache()` / `isComponentBackingEntity(id)` — the two
  read-only predicates the **save** walker uses to mirror
  `destroyAllExceptPreserved`'s exclusion set.
- `entityIdWatermark()` / `advanceEntityIdWatermark(w)` — read/advance the
  monotonic allocator watermark. Load restores exact ids (ids never
  recycle) then advances the watermark past every restored id; new
  allocations can't collide. `advance` never moves the watermark backward.

These are frame-boundary, main-thread-only (asserted), like the rest of the
eager mutation API.

## Scene-transition reset (`resetGameplay`)

`IREntity::resetGameplay()` (#1814) is the scene-transition teardown
primitive: it destroys every live **gameplay** entity while preserving the
engine's infrastructure entities, leaving the world immediately usable for the
next scene (the contrast with `destroyAllEntities`, which tears down
*everything* and is end-of-world / test-teardown only).

**Preserve-by-default — three categories survive a reset:**

1. **Singleton entities** — everything in the singleton cache
   (`m_singletonEntityByComponent`). The cache IS the preserve registry, so the
   common case ("global game state lives on a singleton component") needs zero
   per-entity bookkeeping. Crucially the cache is **NOT cleared** (unlike
   `destroyAllEntities`), so a surviving singleton keeps its entity id *and its
   value* across the reset.
2. **`C_Persistent`-tagged entities** — the opt-out for a non-singleton entity
   that must outlive a reset. The RenderManager stamps `C_Persistent` on its
   camera + framebuffer/canvas entities at construction
   (`render_manager.cpp`), so the render context survives. Any engine- or
   creation-created non-singleton entity that must persist needs this tag.
3. **Component-type backing entities** — each registered component is itself
   backed by an entity id; these stay alive so the next scene's
   `createEntity<T>` keeps working.

The low-level primitive is `EntityManager::destroyAllExceptPreserved(
preserveMarkers)` — generic over a list of preserve-marker `ComponentId`s, so
`engine/entity/` carries no dependency on the prefab-layer `C_Persistent`. The
`C_Persistent` policy lives in the `IREntity::resetGameplay` facade.

**Ordering contract.** Drive a scene swap at a **frame boundary** (not mid-tick
— `resetGameplay` eager-destroys on a snapshot, mirroring `destroyAllEntities`;
calling it inside a `forEachComponent` / parallel group is UB). The scene
machine does, within one boundary: `resetGameplay()` → re-register the next
scene's pipelines (`IRSystem::clearPipeline` / `registerPipeline`) → spawn the
next scene's entities. Lua: `IRWorld.resetGameplay()` + `IRSystem.clearPipeline`.

**Gotchas:**

- **Dangling EntityIds.** A surviving singleton/persistent entity holding the
  `EntityId` of a destroyed gameplay entity goes stale. The modifier
  pre-destroy hook auto-sweeps *modifiers*; arbitrary id fields are not swept —
  re-acquire ids after the next scene builds, or null them in a pre-destroy hook.
- **Named entities are pruned.** `destroyEntity` does not remove `m_namedEntities`
  entries, so `resetGameplay` prunes every name pointing at a now-dead id
  (otherwise `getEntityByName` would assert on the corpse). Surviving entities
  keep their names.
- **System-internal / Lua-global state persists** — systems are never destroyed
  and the Lua VM is not reset. Keep per-scene state in components (destroyed on
  reset), not in system statics or Lua globals.
- **Idempotency is a count, not ids.** Entity ids never recycle (atomic
  counter), so assert on live-entity *count* (and resource counters) returning
  to baseline across cycles, never on id values.
- **CHILD_OF relation entities for preserved entities are destroyed.** A
  `CHILD_OF` relation is itself an ECS entity and is NOT in any preserve
  category, so `destroyAllExceptPreserved` destroys it even when both the
  parent and child survive a reset. The relation remains functionally correct
  because entity ids never recycle — the zombie relation id can never become a
  real entity. Do not call `entityExists(relationId)` expecting `true` after a
  reset; use `getParentEntityFromArchetype` to query the relationship instead.

## Position + transform components are automatic

`createEntity(...)` always adds `C_LocalTransform` and
`C_WorldTransform`. You cannot opt out, but the free-function
wrapper detects when the caller passes one of these types
explicitly and skips the matching default — so
`createEntity(C_LocalTransform{...})` lands the caller's value
rather than emplacing a duplicate column row.

Rendered position lives in `C_WorldTransform.translation_`,
composed by `SYSTEM_PROPAGATE_TRANSFORM` from `C_LocalTransform`
plus the parent chain — see `engine/prefabs/irreden/common/CLAUDE.md`
"SQT transform pair + propagation" for the formula and pipeline
placement. Per-frame additive offsets travel through the modifier
framework's `TRANSFORM_TRANSLATION` / `TRANSFORM_SCALE` vec3 fields;
entities that don't push offsets don't need `C_Modifiers`. The
legacy `C_Position3D` / `C_PositionGlobal3D` / `C_Rotation`
components and their writer chain were retired in T-302.

## Relations

`setParent<ParentKind>(child, parent)` creates a `CHILD_OF` relation. Query
with `queryArchetypeNodesRelational(relation, include, exclude)`. Relations
are implemented as special component-like entries in the archetype, so
adding a relation moves the entity to a different archetype node.

## Gotchas

- **EntityId pointer/index instability.** Don't store a raw pointer or
  column index into a node across a frame; the entity may have moved.
  Store the `EntityId` and re-look-up.
- **No bulk remove.** `removeComponentsSimple()` just loops
  `removeComponent`, which is O(entities × archetype mutations). Prefer
  the deferred API and one flush.
- **Component registration is lazy.** The first use of a component type
  auto-registers it; if you want a specific id, register explicitly before
  any `createEntity` call.
- **Global manager lifetime.** `g_entityManager` is valid only while the
  `World` is alive. Don't capture it in lambdas that outlive the loop.

