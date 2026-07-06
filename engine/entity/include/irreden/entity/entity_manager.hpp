#ifndef ENTITY_MANAGER_H
#define ENTITY_MANAGER_H

#include <irreden/ir_profile.hpp>

#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/entity/archetype_node.hpp>
#include <irreden/entity/archetype_graph.hpp>
#include <irreden/entity/archetype.hpp>

#include <algorithm>
#include <atomic>
#include <functional>
#include <initializer_list>
#include <set>
#include <span>

// TODO: a component should be registered with a size so it
// can be copied around generically just as data.

// Entities can be grouped in their archetype nodes with
// a ChildOf ____ component, where a child of a particular parent
// is a unique component type.

namespace IREntity {

struct EntityRecord {
    ArchetypeNode *archetypeNode;
    int row;
};

struct PendingComponentRemoval {
    EntityId entity_;
    ComponentId componentType_;
};

using PreDestroyHook = std::function<void(EntityId)>;
using PreDestroyHookId = std::uint32_t;
inline constexpr PreDestroyHookId kInvalidPreDestroyHookId = 0;

struct PreDestroyHookEntry {
    PreDestroyHookId id_;
    PreDestroyHook hook_;
};

// T-225: per-worker staging buffer for deferred structural mutations
// produced from a `PARALLEL_FOR` system body. One instance per worker
// (index 0 == main thread, 1..N == IRJob worker threads). Workers
// write only their own slot, so no lock is needed on the producer side;
// `flushStructuralChanges` drains every buffer serially on the main
// thread in `workerId` order so the visible effect is deterministic.
struct WorkerStaging {
    std::vector<PendingComponentRemoval> componentRemovals_;
    std::vector<std::function<void()>> structuralChanges_;
    std::vector<EntityId> markedForDeletion_;
};

class EntityManager {
  public:
    EntityManager();
    ~EntityManager();

    /// Size the per-worker staging vector to `workerSlots` entries
    /// (slot 0 == main thread, slots 1..N == IRJob workers, so the
    /// caller passes `IRJob::workerCount() + 1`). Must be called
    /// once, on the main thread, after `JobManager` is constructed
    /// and before any worker dispatches into the staging path. Safe
    /// to call before — the manager initialises with a single
    /// main-thread slot so pre-`JobManager` deferred ops keep
    /// working unmodified.
    void resizeWorkerStaging(std::size_t workerSlots);

    inline Archetype &getEntityArchetype(EntityId e) {
        return getRecord(e).archetypeNode->type_;
    }
    inline ArchetypeNode *findArchetypeNode(const Archetype &type) {
        return m_archetypeGraph.findArchetypeNode(type);
    }
    inline const std::vector<smart_ArchetypeNode> &getArchetypeNodes() {
        return m_archetypeGraph.getArchetypeNodes();
    }
    inline const ArchetypeGraph *getArchetypeGraph() const {
        return &m_archetypeGraph;
    }
    inline EntityId entityBits(EntityId entity) {
        return entity & IR_ENTITY_ID_BITS;
    }

    [[nodiscard]] bool entityExists(EntityId entity) const {
        return m_entityIndex.contains(entity & IR_ENTITY_ID_BITS);
    }

    // --- World-snapshot restore surface (persist P2, #2213) ---
    // Public wrapper over the archetype graph's node creation. The loader
    // must materialize the target node for a restored archetype that no
    // live entity currently occupies — findArchetypeNode alone can only
    // find an existing one.
    inline ArchetypeNode *findCreateArchetypeNode(const Archetype &type) {
        return m_archetypeGraph.findCreateArchetypeNode(type);
    }

    // Insert a batch of entities carrying their exact saved EntityIds into
    // `node`, appending to its entity list and advancing length_ / the live
    // count. Component columns are filled separately by the caller (the save
    // registry's per-column readers) in the SAME entity order, so on return
    // each column is one batch short of length_ until the caller appends —
    // the caller mirrors the eager path's end-of-node sync assert. Ids go in
    // masked (no flags): the snapshot walker excludes relation/system
    // entities, so a restored gameplay id never carries flag bits.
    // Main-thread only (load is a frame-boundary op).
    void restoreEntitiesBatch(ArchetypeNode *node, std::span<const EntityId> entityIds);

    // Read-only view of the singleton cache (ComponentId -> owning entity).
    // The snapshot walker excludes these entities from the ARCH chunk (they
    // ride the SNGL chunk by value) — one arm of the resetGameplay-mirroring
    // save-exclusion set.
    inline const std::unordered_map<ComponentId, EntityId> &singletonEntityCache() const {
        return m_singletonEntityByComponent;
    }

    // True if `entity` is a component-TYPE backing entity (each registered
    // component is itself an entity id). Another arm of the save-exclusion
    // set mirroring destroyAllExceptPreserved.
    inline bool isComponentBackingEntity(EntityId entity) const {
        return m_pureComponentVectors.contains(entity & IR_ENTITY_ID_BITS);
    }

    // The next-EntityId watermark (the id a subsequent createEntity would
    // allocate). Written to the snapshot META chunk; a load pushes the
    // allocator past every restored id via advanceEntityIdWatermark. Ids
    // never recycle, so this is the whole of restore collision safety.
    inline EntityId entityIdWatermark() const {
        return m_nextEntityId.load(std::memory_order_relaxed);
    }

    // Advance the allocator watermark to at least `watermark` — never
    // backwards, since this session's infrastructure entities may already
    // sit above a cross-session saved watermark. Main-thread only.
    void advanceEntityIdWatermark(EntityId watermark);
    EntityRecord &getRecord(EntityId entity);
    EntityId setFlags(EntityId entity, EntityId flags);
    bool isPureComponent(ComponentId component);
    bool isChildOfRelation(RelationId relation);
    smart_ComponentData createComponentDataVector(ComponentId component);
    void removeComponentById(EntityId entity, ComponentId componentType);
    void destroyEntity(EntityId entity);
    void destroyAllEntities();
    /// Scene-transition teardown (#1814): destroy every live entity EXCEPT
    /// (1) singleton entities (everything in `m_singletonEntityByComponent`)
    /// and (2) entities holding any of the `preserveMarkers` component types.
    /// Unlike `destroyAllEntities`, the singleton cache is NOT cleared — the
    /// preserved singletons stay valid for the next scene. Eager + snapshot-
    /// based (mirrors `destroyAllEntities`): main-thread only, must NOT run
    /// mid-iteration (call it at a frame boundary). Stale `m_namedEntities`
    /// entries pointing at destroyed ids are pruned. Generic by design — the
    /// `C_Persistent` policy lives in the `IREntity::resetGameplay` facade so
    /// this low-level module stays free of any prefab-component dependency.
    void destroyAllExceptPreserved(const std::vector<ComponentId> &preserveMarkers);
    void markEntityForDeletion(EntityId &entity);
    void destroyMarkedEntities();
    NodeId getParentNodeFromRelation(RelationId relation);
    EntityId getRelatedEntityFromArchetype(Archetype type, Relation relation);
    EntityId getParentEntityFromArchetype(const Archetype &type);
    RelationId registerRelation(Relation relation, EntityId relatedEntity);
    void setName(EntityId entity, const std::string &name);
    EntityId getEntityByName(const std::string &name) const;
    // Non-asserting existence check (getEntityByName asserts on a miss).
    bool hasName(const std::string &name) const {
        return m_namedEntities.contains(name);
    }

    template <typename... Components> EntityId createEntity(const Components &...components) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        // T-225: a `createEntity` from a `PARALLEL_FOR` worker body
        // routes through the per-worker staging buffer. We pre-allocate
        // the EntityId atomically so the caller can return it
        // immediately; the archetype-node insertion runs on the main
        // thread at the next `flushStructuralChanges`.
        if (!isMainThreadForDeferred()) {
            EntityId entity = allocateEntityIdAtomic();
            int slot = workerSlotForCurrentThread();
            m_workerStaging[slot].structuralChanges_.push_back([this, entity, components...]() {
                insertReservedEntity(entity, components...);
            });
            return entity;
        }
        EntityId entity = allocateEntity();
        Archetype archetype = getArchetype<Components...>();
        ArchetypeNode *archetypeNode = m_archetypeGraph.findCreateArchetypeNode(archetype);
        int index = insertEntityToNode(archetypeNode, entity, components...);
        updateRecord(entity, archetypeNode, index);
        IRE_LOG_DEBUG(
            "Created entity={} with archetype={}",
            entity,
            makeComponentStringInternal(archetype).c_str()
        );
        return entity;
    }

    // T-225: main-thread completion of a worker-deferred createEntity.
    // The EntityId was already allocated atomically when the worker
    // staged the request; we just complete the archetype-node insertion.
    template <typename... Components>
    void insertReservedEntity(EntityId entity, const Components &...components) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        m_entityIndex.emplace(entity & IR_ENTITY_ID_BITS, EntityRecord{nullptr, -1});
        ++m_liveEntityCount;
        Archetype archetype = getArchetype<Components...>();
        ArchetypeNode *archetypeNode = m_archetypeGraph.findCreateArchetypeNode(archetype);
        int index = insertEntityToNode(archetypeNode, entity, components...);
        updateRecord(entity, archetypeNode, index);
    }

    template <typename Component, typename... Args> ComponentId registerComponent(Args &&...args) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        std::string typeName = typeid(Component).name();
        IR_ASSERT(
            m_pureComponentTypes.find(typeName) == m_pureComponentTypes.end(),
            "Regestering the same component twice"
        );
        ComponentId componentId =
            registerComponentImpl(typeName, std::make_unique<IComponentDataImpl<Component>>());
        IRE_LOG_INFO(
            "Regestered component type={}, sizeof={} with id={}",
            typeName,
            sizeof(Component),
            static_cast<int>(componentId)
        );
        return componentId;
    }

    // Non-template parallel of registerComponent<T>. Registers a runtime-
    // declared component (typically Lua-defined) given a fully-constructed
    // IComponentData impl. The typeName is the user-visible name used to
    // detect duplicates and to look up the ComponentId by name later.
    // Registration is lazy / one-time: registering the same name twice
    // returns kNullComponent (the caller is expected to surface a clear
    // error to the script). The impl pointer must outlive the EntityManager.
    ComponentId registerComponentDynamic(const std::string &typeName, smart_ComponentData impl);

    // True if `typeName` is already registered (template OR dynamic path).
    bool isComponentRegistered(const std::string &typeName) const {
        return m_pureComponentTypes.contains(typeName);
    }

    ComponentId getComponentTypeByName(const std::string &typeName) const {
        auto it = m_pureComponentTypes.find(typeName);
        if (it == m_pureComponentTypes.end())
            return kNullComponent;
        return it->second;
    }

    // Add a runtime-registered component to `entity` using its impl's
    // appendDefaultRow(). Used by the Lua-driven add path; C++ callers
    // should keep using the templated setComponent<T>(entity, value)
    // form so they pass an explicit value.
    void addComponentDynamic(EntityId entity, ComponentId componentType);

    // Remove a runtime-registered component (alias of removeComponentById
    // for symmetry with addComponentDynamic — same machinery as the
    // template removeComponent<T>).
    void removeComponentDynamic(EntityId entity, ComponentId componentType) {
        removeComponentById(entity, componentType);
    }

    // Returns (impl, row) for the given (entity, componentType). nullptr
    // impl if the entity does not currently have this component. Used by
    // dynamic readers/writers (Lua-defined component access path) since
    // the templated getComponent<T>() requires a static C++ type.
    std::pair<IComponentData *, int>
    getComponentDataAndRow(EntityId entity, ComponentId componentType);

    bool hasComponent(EntityId entity, ComponentId componentType);

    template <typename Component> ComponentId getComponentType() {
        std::string typeName = typeid(Component).name();
        if (m_pureComponentTypes.find(typeName) == m_pureComponentTypes.end()) {
            registerComponent<Component>();
        }
        return m_pureComponentTypes[typeName];
    }

    // Insert (or overwrite) a component on an entity. Does NOT require the
    // component to be default-constructible: the new-archetype path moves
    // the entity, then push_backs the caller's value directly into the
    // new column. Lets components like C_CanvasAOTexture `= delete` their
    // default ctor and force a size-bearing ctor at the call site.
    template <typename Component>
    Component &setComponent(EntityId entity, const Component &component) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        EntityRecord &record = getRecord(entity);
        ComponentId componentType = getComponentType<Component>();

        if (!record.archetypeNode->type_.contains(componentType)) {
            return insertNewComponent<Component>(record, component);
        }

        IComponentDataImpl<Component> *data = castComponentDataPointer<Component>(
            record.archetypeNode->components_[componentType].get()
        );
        Component &c = data->dataVector[record.row];
        c = component;
        return c;
    }

    template <typename Component>
    Component &insertNewComponent(EntityRecord &record, const Component &component) {
        ComponentId componentType = getComponentType<Component>();
        ArchetypeNode *fromNode = record.archetypeNode;
        Archetype type = fromNode->type_;
        type.insert(componentType);
        ArchetypeNode *toNode = m_archetypeGraph.findCreateArchetypeNode(type);

        moveEntityByArchetype(record, fromNode->type_, fromNode, toNode);

        IComponentDataImpl<Component> *data =
            castComponentDataPointer<Component>(toNode->components_[componentType].get());
        data->dataVector.push_back(component);

        IR_ASSERT(
            static_cast<int>(data->dataVector.size()) == toNode->length_,
            "Component column out of sync with archetype node row count "
            "after push_back into new archetype column."
        );

        IRE_LOG_DEBUG(
            "Added component type={} to entity={}: \n\
                \tnew row={}\n\
                \tnew type={}",
            componentType,
            toNode->entities_[record.row],
            record.row,
            makeComponentStringInternal(type).c_str()
        );

        return data->dataVector[record.row];
    }

    EntityId setRelation(Relation relation, EntityId entity, EntityId relatedEntity);

    template <typename... Components>
    void setComponents(EntityId entity, const Components &...components) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        /* This solution was found here:
         * https://kubasejdak.com/techniques-of-variadic-templates#non-recursive-argument-evaluation-with-stdinitializer-list
         * Essentially, allows pack expanstion, and the initalizer list ends up full of zeros,
         * and should get optimized away. This is to avoid recursion and the associated overhead
         * of creating extra template specializations.
         */
        std::initializer_list<int>{(setComponent(entity, components), 0)...};
    }

    template <typename... Components> Archetype getArchetype() {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        Archetype res{getComponentType<Components>()...};
        return res;
    }

    template <typename Component> void removeComponent(EntityId entity) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        removeComponentById(entity, getComponentType<Component>());
    }

    template <typename Component> void removeComponentDeferred(EntityId entity) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        // T-225: route through the per-worker staging buffer so a
        // `PARALLEL_FOR` body in any worker can call this without
        // racing on the shared pending vector.
        int slot = workerSlotForCurrentThread();
        m_workerStaging[slot].componentRemovals_.push_back({
            entity,
            getComponentType<Component>(),
        });
    }

    template <typename Component>
    void setComponentDeferred(EntityId entity, const Component &component) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        // T-225: route through the per-worker staging buffer (see
        // `removeComponentDeferred`).
        int slot = workerSlotForCurrentThread();
        m_workerStaging[slot].structuralChanges_.push_back([this, entity, component]() {
            if (entityExists(entity)) {
                setComponent<Component>(entity, component);
            }
        });
    }

    void flushStructuralChanges();

    template <typename Component> Component &getComponent(EntityId entity) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        const EntityRecord &record = getRecord(entity);
        ArchetypeNode *node = record.archetypeNode;
        Archetype archetype = node->type_;
        ComponentId componentType = getComponentType<Component>();

        IR_ASSERT(
            std::find(archetype.begin(), archetype.end(), componentType) != archetype.end(),
            "Attempted to retrieve non-existant component {} from entity {}",
            componentType,
            entity

        );
        IComponentDataImpl<Component> *data =
            castComponentDataPointer<Component>(node->components_[componentType].get());

        return data->dataVector[record.row];
    }

    template <typename Component> std::optional<Component *> getComponentOptional(EntityId entity) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        if (entity == kNullEntity) {
            return std::nullopt;
        }

        const EntityRecord &record = getRecord(entity);
        ArchetypeNode *node = record.archetypeNode;
        Archetype archetype = node->type_;
        ComponentId componentType = getComponentType<Component>();

        if (std::find(archetype.begin(), archetype.end(), componentType) == archetype.end()) {
            return std::nullopt;
        }
        IComponentDataImpl<Component> *data =
            castComponentDataPointer<Component>(node->components_[componentType].get());
        return std::make_optional(&data->dataVector[record.row]);
    }

    template <typename Component> std::vector<Component> &getComponentData(ArchetypeNode *node) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        Archetype archetype = node->type_;
        ComponentId componentType = getComponentType<Component>();

        IR_ASSERT(
            std::find(archetype.begin(), archetype.end(), componentType) != archetype.end(),
            "Attempted to retrieve non-existant component vector from node: archetype={}, "
            "componentType={}",
            makeComponentStringInternal(archetype).c_str(),
            componentType
        );
        IComponentDataImpl<Component> *data =
            castComponentDataPointer<Component>(node->components_[componentType].get());

        return data->dataVector;
    }

    template <typename... Components>
    std::vector<EntityId> createEntitiesBatch(const std::vector<Components> &...componentVectors) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        std::vector<EntityId> res;
        size_t sizes[] = {componentVectors.size()...};
        IR_ASSERT(
            std::all_of(
                std::begin(sizes),
                std::end(sizes),
                [&sizes](size_t cur) { return cur == sizes[0]; }
            ),
            "Create batch entities with different sized component vectors"
        );
        size_t numEntities = sizes[0];
        for (int i = 0; i < numEntities; i++) {
            res.push_back(allocateEntity());
        }
        Archetype archetype = getArchetype<Components...>();
        ArchetypeNode *toNode = m_archetypeGraph.findCreateArchetypeNode(archetype);
        std::vector<int> indices = inserComponentsBatch(toNode, res, componentVectors...);
        for (int i = 0; i < numEntities; i++) {
            updateRecord(res[i], toNode, indices[i]);
        }
        return res;
    }

    EntityId getLiveEntityCount() const {
        return m_liveEntityCount;
    }

    /// Register a hook that fires inside `destroyEntity` BEFORE the
    /// entity's components are torn down. The hook receives the
    /// `EntityId` about to be destroyed; the entity and all its
    /// components are still fully readable, and the hook may iterate
    /// other entities (typical use: framework-level sweeps that strip
    /// references to the dying entity from peer entities — e.g. the
    /// modifier framework's source-attribution sweep).
    ///
    /// The hook MUST NOT mutate the about-to-be-destroyed entity's
    /// archetype (no `setComponent` / `removeComponent` on that id);
    /// peer entities are fine to mutate. Hooks fire in registration
    /// order. Returns a token; pass it to `unregisterPreDestroyHook`
    /// to remove. Token `0` is reserved for "invalid".
    PreDestroyHookId registerPreDestroyHook(PreDestroyHook hook);
    void unregisterPreDestroyHook(PreDestroyHookId id);

    /// Singleton-component support. One entity per component type, cached
    /// by `ComponentId`. The cache is lazily validated: if the cached
    /// entity has been destroyed (e.g. by `destroyAllEntities`), the next
    /// lookup discards the stale entry and either lazy-creates (typed
    /// path) or returns `kNullEntity` (or-null path).
    ///
    /// Typed C++ entry point: `singleton<T>()` (templated). Uses
    /// `createEntity(T{})` so `T` must be default-constructible.
    template <typename Component> EntityId getOrCreateSingleton() {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        const ComponentId componentType = getComponentType<Component>();
        auto it = m_singletonEntityByComponent.find(componentType);
        if (it != m_singletonEntityByComponent.end() && entityExists(it->second)) {
            return it->second;
        }
        if (it != m_singletonEntityByComponent.end()) {
            m_singletonEntityByComponent.erase(it);
        }
        EntityId entity = createEntity(Component{});
        m_singletonEntityByComponent[componentType] = entity;
        return entity;
    }

    /// Untyped entry point used by Lua-defined components — the component
    /// is attached via the dynamic-add path so this works uniformly for
    /// both `registerComponent<T>` and `registerComponentDynamic` types.
    /// For typed C++ components, prefer `getOrCreateSingleton<T>()`.
    EntityId getOrCreateSingletonByComponentId(ComponentId componentType);

    /// No-create variant. Returns the cached singleton entity for this
    /// component type if previously created (and still alive), else
    /// `kNullEntity`. Useful for query sites that should not implicitly
    /// instantiate the singleton when called before initial setup.
    EntityId getSingletonByComponentIdOrNull(ComponentId componentType);

  private:
    std::unordered_map<EntityId, EntityRecord> m_entityIndex;
    ArchetypeGraph m_archetypeGraph;
    std::unordered_map<std::string, ComponentId> m_pureComponentTypes;
    std::unordered_map<EntityId, RelationId> m_parentRelations;
    std::unordered_map<RelationId, EntityId> m_childOfRelations;
    std::unordered_map<ComponentId, smart_ComponentData> m_pureComponentVectors;
    // TODO: Remove when entity is destroyed
    std::unordered_map<std::string, EntityId> m_namedEntities;
    EntityId m_liveEntityCount;
    // Legacy main-thread deferred-mutation queues. After T-225 all
    // public API writes go through m_workerStaging[0] (main thread)
    // or m_workerStaging[slot] (workers), so these will always be
    // empty in a correctly-running post-T-225 World. Retained as a
    // safety net: flushStructuralChanges / destroyMarkedEntities drain
    // them first, so any internal path that bypasses the public API
    // (migration glue, future low-level ECS work) still gets picked up.
    std::vector<EntityId> m_entitiesMarkedForDeletion;
    std::vector<PendingComponentRemoval> m_pendingComponentRemovals;
    std::vector<std::function<void()>> m_pendingStructuralChanges;
    // T-225: per-worker staging buffers for deferred mutations from
    // worker threads. Sized to `IRJob::workerCount() + 1` by
    // `resizeWorkerStaging` after `JobManager` is constructed. Until
    // then the vector has a single slot for the main thread, so the
    // pre-`JobManager` path (engine init, tests with no worker pool)
    // works unchanged.
    std::vector<WorkerStaging> m_workerStaging;
    // T-225: monotonic atomic counter for cross-thread EntityId
    // allocation. Replaces the legacy `std::queue<EntityId>` pool —
    // worker threads can `fetch_add` without contending on a mutex.
    // IDs are NOT recycled (the pool semantics are gone); the
    // 25-bit `IR_ENTITY_ID_BITS` space gives ~33M entities per
    // session, plenty for current workloads. If a long-running
    // session ever approaches the cap, switch to a tiered allocator.
    std::atomic<EntityId> m_nextEntityId{IR_RESERVED_ENTITIES};
    std::vector<PreDestroyHookEntry> m_preDestroyHooks;
    PreDestroyHookId m_nextPreDestroyHookId{1};
    bool m_preDestroyHookIterating{false};
    // Singleton-component cache. Key is `ComponentId`, value is the entity
    // owning that component. Stale entries (entity destroyed externally)
    // are evicted lazily via the `entityExists` check on lookup. Cleared
    // unconditionally by `destroyAllEntities`.
    std::unordered_map<ComponentId, EntityId> m_singletonEntityByComponent;

    EntityId allocateEntity();
    EntityId allocateEntityIdAtomic();
    int workerSlotForCurrentThread() const;
    bool isMainThreadForDeferred() const;
    void addNewEntityToBaseNode(EntityId entity);
    void returnEntityToPool(EntityId entity);
    ComponentId registerComponentImpl(const std::string &typeName, smart_ComponentData impl);
    void addComponentByIdImpl(EntityRecord &record, ComponentId componentType);
    void pushCopyData(
        IComponentData *fromStructure, unsigned int fromIndex, IComponentData *toStructure
    );
    int moveEntityByArchetype(
        EntityRecord &entity, const Archetype &type, ArchetypeNode *fromNode, ArchetypeNode *toNode
    );
    void handleComponentMove(
        const ComponentId component,
        ArchetypeNode *fromNode,
        ArchetypeNode *toNode,
        const unsigned int row
    );
    void removeEntityFromArchetypeNode(ArchetypeNode *node, unsigned int index);
    void
    handleComponentRemove(const ComponentId component, ArchetypeNode *node, const unsigned int row);
    void updateBackEntityPosition(ArchetypeNode *node, unsigned int newPos);
    void removeComponentById(
        EntityRecord &record,
        EntityId entity,
        ComponentId componentType,
        ArchetypeNode *fromNode,
        ArchetypeNode *toNode
    );
    void destroyComponents(EntityId entity);
    void destroyComponent(ComponentId component, ArchetypeNode *node, unsigned int row);
    void updateRecord(EntityId entity, ArchetypeNode *node, unsigned int row);
    void insertRelation(EntityId entity, RelationId relation);

    template <typename Component, typename... Args>
    int emplaceComponent(IComponentData *dest, Args &&...args) {
        IComponentDataImpl<Component> *d = castComponentDataPointer<Component>(dest);
        d->dataVector.emplace_back(std::forward<Args>(args)...);
        int index = d->size() - 1;
        return index;
    }

    template <typename... Components>
    int
    insertEntityToNode(ArchetypeNode *toNode, EntityId entity, const Components &...components) {
        int newIndex = toNode->length_;

        std::tuple<IComponentDataImpl<Components> *...> dataPointers = {
            castComponentDataPointer<Components>(
                toNode->components_[getComponentType<Components>()].get()
            )...
        };

        std::apply(
            [&](auto &&...args) { (args->dataVector.emplace_back(components), ...); },
            dataPointers
        );

        toNode->entities_.push_back(entity);
        toNode->length_++;

        return newIndex;
    }

    template <typename... Components>
    std::vector<int> inserComponentsBatch(
        ArchetypeNode *toNode,
        const std::vector<EntityId> &entityIds,
        const std::vector<Components> &...componentVectors
    ) {
        std::vector<int> indices;
        std::tuple<IComponentDataImpl<Components> *...> dataPointers = {
            castComponentDataPointer<Components>(
                toNode->components_[getComponentType<Components>()].get()
            )...
        };
        int numEntities = entityIds.size();
        for (int i = 0; i < numEntities; i++) {
            indices.push_back(toNode->length_);
            toNode->length_++;
            toNode->entities_.push_back(entityIds[i]);
            std::apply(
                [&](auto &&...args) { (args->dataVector.emplace_back(componentVectors[i]), ...); },
                dataPointers
            );
        }
        return indices;
    }
};

} // namespace IREntity

// #include <irreden/entity/entity_manager.tpp>

#endif /* ENTITY_MANAGER_H */
