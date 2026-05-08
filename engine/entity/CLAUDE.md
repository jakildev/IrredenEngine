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
- The cost is O(hooks × destructions); each hook should be O(world)
  at worst. Don't register hooks that run an expensive search per
  destroy.

The framework-level use case is wiring engine-wide invariants (modifier
sweeps, owned-resource cleanup) that would otherwise force every caller
to remember a manual cleanup step before `destroyEntity`. Per-component
cleanup belongs in the component's `onDestroy()` member, not in a hook
— see `engine/prefabs/CLAUDE.md` "Documented exceptions".

## Position components are automatic

`createEntity(...)` always adds `C_PositionGlobal3D` and
`C_PositionOffset3D`. You cannot opt out. If you want an entity at the
origin, you still pay for the two vec3s.

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

