#include <irreden/ir_profile.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_job.hpp>

#include <irreden/entity/entity_manager.hpp>
#include <irreden/job/job_manager.hpp>

#include <memory>
#include <unordered_set>

namespace IREntity {

EntityManager::EntityManager()
    : m_entityIndex{}
    , m_archetypeGraph{}
    , m_pureComponentTypes{}
    , m_pureComponentVectors{}
    , m_liveEntityCount{0}
    , m_entitiesMarkedForDeletion{}
    , m_pendingComponentRemovals{}
    , m_workerStaging(1) {
    // T-225: start with a single staging slot for the main thread.
    // `resizeWorkerStaging` grows the vector to
    // `IRJob::workerCount() + 1` once `JobManager` is constructed.
    g_entityManager = this;
    IRE_LOG_INFO("Created EntityManager (IR_MAX_ENTITIES={})", static_cast<int>(IR_MAX_ENTITIES));
}

void EntityManager::resizeWorkerStaging(std::size_t workerSlots) {
    IR_ASSERT(
        workerSlots >= 1,
        "EntityManager::resizeWorkerStaging requires at least one slot (main thread)"
    );
    if (workerSlots <= m_workerStaging.size()) {
        return;
    }
    m_workerStaging.resize(workerSlots);
}

EntityManager::~EntityManager() {
    if (g_entityManager == this) {
        g_entityManager = nullptr;
    }
}

EntityId EntityManager::allocateEntity() {
    EntityId id = allocateEntityIdAtomic();
    m_entityIndex.emplace(id & IR_ENTITY_ID_BITS, EntityRecord{nullptr, -1});
    m_liveEntityCount++;

    IRE_LOG_DEBUG("Created entity={}", id);
    return id;
}

EntityId EntityManager::allocateEntityIdAtomic() {
    // T-225: ID allocation is the only contended op when workers spawn
    // entities from a `PARALLEL_FOR` body. `fetch_add` on `m_nextEntityId`
    // is cheap (one CAS-loop iteration on x86, single RMW on Apple
    // Silicon) and amortised over per-row work it is invisible. IDs are
    // monotonically allocated — no recycle pool — so the `IR_ENTITY_ID_BITS`
    // (25-bit) space gives ~33M entities per session.
    EntityId id = m_nextEntityId.fetch_add(1, std::memory_order_relaxed);
    IR_ASSERT(id < IR_MAX_ENTITIES, "Max entity size reached");
    return id;
}

void EntityManager::restoreEntitiesBatch(ArchetypeNode *node, std::span<const EntityId> entityIds) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    IR_ASSERT(
        isMainThreadForDeferred(),
        "restoreEntitiesBatch is a frame-boundary op — must run on the main thread"
    );
    for (const EntityId entity : entityIds) {
        const EntityId masked = entity & IR_ENTITY_ID_BITS;
        m_entityIndex.emplace(masked, EntityRecord{node, node->length_});
        node->entities_.push_back(masked);
        node->length_++;
        ++m_liveEntityCount;
    }
}

void EntityManager::advanceEntityIdWatermark(EntityId watermark) {
    IR_ASSERT(
        isMainThreadForDeferred(),
        "advanceEntityIdWatermark is a frame-boundary op — must run on the main thread"
    );
    if (watermark > m_nextEntityId.load(std::memory_order_relaxed)) {
        m_nextEntityId.store(watermark, std::memory_order_relaxed);
    }
}

bool EntityManager::isMainThreadForDeferred() const {
    // No JobManager → unit tests + pre-`World` startup → always main.
    if (g_jobManager == nullptr) {
        return true;
    }
    return g_jobManager->isMainThread();
}

int EntityManager::workerSlotForCurrentThread() const {
    // Slot 0 is main; 1..workerCount() are IRJob workers. Out-of-range
    // wid is unreachable in a correctly configured World — it guards
    // against misconfigured setups where resizeWorkerStaging was never
    // called. Falling back to slot 0 routes the write to the main-thread
    // staging buffer, which is safe because the scheduler guarantees
    // workers and flushStructuralChanges never run concurrently.
    if (g_jobManager == nullptr) {
        return 0;
    }
    const int wid = g_jobManager->workerId();
    if (wid < 0 || static_cast<std::size_t>(wid) >= m_workerStaging.size()) {
        return 0;
    }
    return wid;
}

void EntityManager::addNewEntityToBaseNode(EntityId id) {
    ArchetypeNode *node = m_archetypeGraph.getBaseNode();
    m_entityIndex.emplace(id & IR_ENTITY_ID_BITS, EntityRecord{node, node->length_});
    node->entities_.push_back(id);
    node->length_++;
}

void EntityManager::returnEntityToPool(EntityId entity) {
    // T-225: the recycle pool is gone (replaced by the atomic
    // `m_nextEntityId` counter). Destroy just removes the entity
    // from the index — the ID itself is permanently retired.
    m_entityIndex.erase(entity & IR_ENTITY_ID_BITS);
    --m_liveEntityCount;
}

EntityId EntityManager::setFlags(EntityId entity, EntityId flags) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    EntityRecord &record = getRecord(entity);
    // Make sure ID bits are not getting modified
    record.archetypeNode->entities_.at(record.row) |= (flags & (~IR_ENTITY_ID_BITS));
    return record.archetypeNode->entities_.at(record.row);
}

void EntityManager::markEntityForDeletion(EntityId &entity) {
    // T-225: route into the per-worker buffer. Workers write only
    // their own slot; the flag bit on the caller's `entity` ref is
    // the caller's memory, not ours. `destroyMarkedEntities`
    // drains every slot serially on the main thread.
    int slot = workerSlotForCurrentThread();
    m_workerStaging[slot].markedForDeletion_.push_back(entity);
    entity |= IR_ENTITY_FLAG_MARKED_FOR_DELETION;
}

/* TODO: destroy entities in batch after each frame */
void EntityManager::destroyEntity(EntityId entity) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    // Pre-destroy hooks run before component teardown so callbacks see
    // the entity (and its peers) in their final fully-valid state.
    // Hooks must not unregister hooks during this loop — see the
    // assert in unregisterPreDestroyHook.
    m_preDestroyHookIterating = true;
    for (std::size_t i = 0; i < m_preDestroyHooks.size(); ++i) {
        m_preDestroyHooks[i].hook_(entity);
    }
    m_preDestroyHookIterating = false;
    EntityRecord &record = getRecord(entity);
    IRE_LOG_DEBUG("entity={}, record.row={}", entity, record.row);
    ArchetypeNode *node = record.archetypeNode;
    destroyComponents(entity);
    removeEntityFromArchetypeNode(node, record.row);
    returnEntityToPool(entity);
    IRE_LOG_DEBUG("Destroyed entity {}", entity & IR_ENTITY_ID_BITS);
}

PreDestroyHookId EntityManager::registerPreDestroyHook(PreDestroyHook hook) {
    IR_ASSERT(static_cast<bool>(hook), "registerPreDestroyHook called with empty hook");
    PreDestroyHookId id = m_nextPreDestroyHookId++;
    m_preDestroyHooks.push_back(PreDestroyHookEntry{id, std::move(hook)});
    return id;
}

void EntityManager::unregisterPreDestroyHook(PreDestroyHookId id) {
    IR_ASSERT(
        !m_preDestroyHookIterating,
        "unregisterPreDestroyHook called from inside a pre-destroy hook callback — "
        "this would silently skip a sibling hook. Defer the unregister until destroyEntity returns."
    );
    if (id == kInvalidPreDestroyHookId)
        return;
    auto it = std::find_if(
        m_preDestroyHooks.begin(),
        m_preDestroyHooks.end(),
        [id](const PreDestroyHookEntry &e) { return e.id_ == id; }
    );
    if (it != m_preDestroyHooks.end()) {
        m_preDestroyHooks.erase(it);
    }
}

void EntityManager::destroyComponents(EntityId entity) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    EntityRecord &record = getRecord(entity);
    const Archetype &type = record.archetypeNode->type_;
    for (auto itr = type.begin(); itr != type.end(); itr++) {
        destroyComponent(*itr, record.archetypeNode, record.row);
    }
}

void EntityManager::setName(EntityId entity, const std::string &name) {
    m_namedEntities[name] = entityBits(entity);
}

EntityId EntityManager::getEntityByName(const std::string &name) const {
    if (m_namedEntities.contains(name)) {
        return m_namedEntities.at(name);
    }
    IR_ASSERT(false, "Entity with name {} does not exist", name);
    return kNullEntity;
}

void EntityManager::destroyComponent(
    ComponentId component, ArchetypeNode *node, unsigned int index
) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    if (!isPureComponent(component)) {
        return;
        // IR_ASSERT(false, "non pure components not supported rn");
    }
    node->components_.at(component)->destroy(index);
    return;
}

void EntityManager::destroyMarkedEntities() {
    IR_ASSERT(
        isMainThreadForDeferred(),
        "EntityManager::destroyMarkedEntities must run on the main thread"
    );
    // T-225: drain the legacy main-thread list first (callers that
    // bypass the per-worker buffer — pre-`World` startup, e.g. — still
    // funnel through this vector).
    for (std::size_t i = 0; i < m_entitiesMarkedForDeletion.size(); ++i) {
        this->destroyEntity(m_entitiesMarkedForDeletion.at(i));
    }
    m_entitiesMarkedForDeletion.clear();
    // Then per-worker buffers in workerId order. Deterministic order is
    // required so `--auto-screenshot` reproducibility holds across
    // sessions: the same set of spawn/destroy operations issued from the
    // same workers must produce the same archetype-node row order.
    for (auto &staging : m_workerStaging) {
        for (std::size_t i = 0; i < staging.markedForDeletion_.size(); ++i) {
            this->destroyEntity(staging.markedForDeletion_[i]);
        }
        staging.markedForDeletion_.clear();
    }
}

void EntityManager::removeComponentById(EntityId entity, ComponentId componentType) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    EntityRecord &record = getRecord(entity);
    ArchetypeNode *fromNode = record.archetypeNode;
    Archetype type = fromNode->type_;
    if (std::find(type.begin(), type.end(), componentType) == type.end()) {
        return;
    }

    type.erase(componentType);
    ArchetypeNode *toNode = m_archetypeGraph.findCreateArchetypeNode(type);
    removeComponentById(record, entity, componentType, fromNode, toNode);
}

void EntityManager::flushStructuralChanges() {
    IR_ASSERT(
        isMainThreadForDeferred(),
        "EntityManager::flushStructuralChanges must run on the main thread"
    );
    // T-225: keep looping until every staging buffer (legacy main +
    // per-worker) is empty. A structural-change lambda may queue
    // further changes; the legacy single-buffer flush handled this
    // with a while loop and we preserve that semantics.
    auto haveWork = [this]() {
        if (!m_pendingComponentRemovals.empty() || !m_pendingStructuralChanges.empty()) {
            return true;
        }
        for (const auto &staging : m_workerStaging) {
            if (!staging.componentRemovals_.empty() || !staging.structuralChanges_.empty()) {
                return true;
            }
        }
        return false;
    };

    while (haveWork()) {
        // Coalesce all component removals (legacy + per-worker) into
        // one batch so the existing `removalsByComponentAndNode`
        // grouping still amortises the archetype walk.
        std::vector<PendingComponentRemoval> pendingComponentRemovals =
            std::move(m_pendingComponentRemovals);
        m_pendingComponentRemovals.clear();
        for (auto &staging : m_workerStaging) {
            for (auto &removal : staging.componentRemovals_) {
                pendingComponentRemovals.push_back(removal);
            }
            staging.componentRemovals_.clear();
        }

        using RemovalGroupsByNode = std::unordered_map<ArchetypeNode *, std::vector<EntityId>>;
        std::unordered_map<ComponentId, RemovalGroupsByNode> removalsByComponentAndNode;

        for (const auto &pendingRemoval : pendingComponentRemovals) {
            if (!entityExists(pendingRemoval.entity_)) {
                continue;
            }

            EntityRecord &record = getRecord(pendingRemoval.entity_);
            ArchetypeNode *fromNode = record.archetypeNode;
            if (std::find(
                    fromNode->type_.begin(),
                    fromNode->type_.end(),
                    pendingRemoval.componentType_
                ) == fromNode->type_.end()) {
                continue;
            }

            removalsByComponentAndNode[pendingRemoval.componentType_][fromNode].push_back(
                pendingRemoval.entity_
            );
        }

        for (auto &[componentType, removalsByNode] : removalsByComponentAndNode) {
            for (auto &[fromNode, entities] : removalsByNode) {
                Archetype type = fromNode->type_;
                if (std::find(type.begin(), type.end(), componentType) == type.end()) {
                    continue;
                }

                type.erase(componentType);
                ArchetypeNode *toNode = m_archetypeGraph.findCreateArchetypeNode(type);

                std::sort(entities.begin(), entities.end(), [this](EntityId a, EntityId b) {
                    return getRecord(a).row > getRecord(b).row;
                });

                for (EntityId entity : entities) {
                    if (!entityExists(entity)) {
                        continue;
                    }

                    EntityRecord &record = getRecord(entity);
                    if (record.archetypeNode == fromNode) {
                        removeComponentById(record, entity, componentType, fromNode, toNode);
                        continue;
                    }

                    removeComponentById(entity, componentType);
                }
            }
        }

        // Drain structural-change lambdas in deterministic order:
        // legacy main vector first, then per-worker slots 0..N. The
        // archetype-graph mutations that result MUST be deterministic
        // across runs so `--auto-screenshot` reproducibility holds —
        // running per-worker slots in `workerId` order is the only
        // ordering that satisfies this.
        std::vector<std::function<void()>> pendingStructuralChanges =
            std::move(m_pendingStructuralChanges);
        m_pendingStructuralChanges.clear();
        for (auto &staging : m_workerStaging) {
            for (auto &op : staging.structuralChanges_) {
                pendingStructuralChanges.push_back(std::move(op));
            }
            staging.structuralChanges_.clear();
        }

        for (auto &operation : pendingStructuralChanges) {
            operation();
        }
    }
}

void EntityManager::destroyAllEntities() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);

    flushStructuralChanges();

    // Process pending deferred deletes first.
    destroyMarkedEntities();

    std::vector<EntityId> entitiesToDestroy;
    entitiesToDestroy.reserve(m_entityIndex.size());
    for (const auto &[entityId, _record] : m_entityIndex) {
        entitiesToDestroy.push_back(entityId);
    }

    for (const EntityId entity : entitiesToDestroy) {
        if (m_entityIndex.contains(entity & IR_ENTITY_ID_BITS)) {
            destroyEntity(entity);
        }
    }

    m_singletonEntityByComponent.clear();
}

void EntityManager::destroyAllExceptPreserved(const std::vector<ComponentId> &preserveMarkers) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);

    flushStructuralChanges();
    destroyMarkedEntities();

    // Build the preserve set (masked ids). (1) Every singleton entity — the
    // cache is the authoritative singleton registry. (2) Every entity holding
    // a preserve-marker component, found by scanning the archetype nodes that
    // contain the marker's ComponentId (no per-entity getComponent).
    std::unordered_set<EntityId> preserved;
    // (0) Component-TYPE entities. Each registered component is itself backed
    // by an entity id (registerComponentImpl -> createEntity), so it lives in
    // m_entityIndex. Unlike destroyAllEntities (full teardown), resetGameplay
    // continues into the next scene, so these infrastructure entities MUST
    // survive — otherwise the next scene's createEntity<T> would reference a
    // ComponentId whose backing entity was torn down.
    for (const auto &[componentId, _impl] : m_pureComponentVectors) {
        preserved.insert(componentId & IR_ENTITY_ID_BITS);
    }
    for (const auto &[componentId, singletonEntity] : m_singletonEntityByComponent) {
        if (entityExists(singletonEntity)) {
            preserved.insert(singletonEntity & IR_ENTITY_ID_BITS);
        }
    }
    if (!preserveMarkers.empty()) {
        for (const auto &nodePtr : m_archetypeGraph.getArchetypeNodes()) {
            ArchetypeNode *node = nodePtr.get();
            const bool hasMarker = std::any_of(
                preserveMarkers.begin(),
                preserveMarkers.end(),
                [node](ComponentId marker) { return node->type_.contains(marker); }
            );
            if (!hasMarker) {
                continue;
            }
            for (int i = 0; i < node->length_; ++i) {
                preserved.insert(node->entities_[i] & IR_ENTITY_ID_BITS);
            }
        }
    }

    // Snapshot live ids, then destroy everything not in the preserve set.
    std::vector<EntityId> entitiesToDestroy;
    entitiesToDestroy.reserve(m_entityIndex.size());
    for (const auto &[entityId, _record] : m_entityIndex) {
        if (!preserved.contains(entityId & IR_ENTITY_ID_BITS)) {
            entitiesToDestroy.push_back(entityId);
        }
    }
    for (const EntityId entity : entitiesToDestroy) {
        if (m_entityIndex.contains(entity & IR_ENTITY_ID_BITS)) {
            destroyEntity(entity);
        }
    }

    // Prune stale name->id entries — destroyEntity does not touch
    // m_namedEntities, so a destroyed gameplay entity's name would otherwise
    // resolve (and assert) on a dead id at the next getEntityByName.
    for (auto it = m_namedEntities.begin(); it != m_namedEntities.end();) {
        if (!m_entityIndex.contains(it->second & IR_ENTITY_ID_BITS)) {
            it = m_namedEntities.erase(it);
        } else {
            ++it;
        }
    }

    // Deliberately do NOT clear m_singletonEntityByComponent — singletons
    // survive a gameplay reset (the key difference from destroyAllEntities).
}

EntityId EntityManager::getOrCreateSingletonByComponentId(ComponentId componentType) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    auto it = m_singletonEntityByComponent.find(componentType);
    if (it != m_singletonEntityByComponent.end() && entityExists(it->second)) {
        return it->second;
    }
    if (it != m_singletonEntityByComponent.end()) {
        m_singletonEntityByComponent.erase(it);
    }
    EntityId entity = createEntity();
    addComponentDynamic(entity, componentType);
    m_singletonEntityByComponent[componentType] = entity;
    return entity;
}

EntityId EntityManager::getSingletonByComponentIdOrNull(ComponentId componentType) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    auto it = m_singletonEntityByComponent.find(componentType);
    if (it == m_singletonEntityByComponent.end()) {
        return kNullEntity;
    }
    if (!entityExists(it->second)) {
        m_singletonEntityByComponent.erase(it);
        return kNullEntity;
    }
    return it->second;
}

EntityRecord &EntityManager::getRecord(EntityId entity) {
    return m_entityIndex[entity & IR_ENTITY_ID_BITS];
}

// TODO: make this a flag instead
bool EntityManager::isPureComponent(ComponentId component) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    return m_pureComponentVectors.find(component) != m_pureComponentVectors.end();
}

bool EntityManager::isChildOfRelation(RelationId relation) {
    return m_childOfRelations.contains(relation);
}

NodeId EntityManager::getParentNodeFromRelation(RelationId relation) {
    if (!isChildOfRelation(relation)) {
        return kNullNode;
    }
    return getRecord(m_childOfRelations[relation]).archetypeNode->id_;
}

EntityId EntityManager::getRelatedEntityFromArchetype(Archetype type, Relation relation) {
    if (relation == NONE) {
        return kNullEntity;
    }
    if (relation == CHILD_OF) {
        return getParentEntityFromArchetype(type);
    }
    IR_ASSERT(false, "Unsupported relation: {}", static_cast<int>(relation));
    return kNullEntity;
}

EntityId EntityManager::getParentEntityFromArchetype(const Archetype &type) {
    for (auto relation : type) {
        if (isChildOfRelation(relation)) {
            return m_childOfRelations[relation];
        }
    }
    return kNullEntity;
}

RelationId EntityManager::registerRelation(Relation relation, EntityId relatedEntity) {
    if (relation == CHILD_OF) {
        RelationId newRelation = createEntity();
        setFlags(newRelation, kEntityFlagIsRelation);
        m_parentRelations.insert({entityBits(relatedEntity), newRelation});
        m_childOfRelations.insert({newRelation, entityBits(relatedEntity)});
        IRE_LOG_INFO(
            "Regestered relation type={} id={} related to entity={}",
            static_cast<int>(relation),
            static_cast<int>(newRelation),
            static_cast<int>(relatedEntity)
        );
        return newRelation;
    }

    IR_ASSERT(false, "Unsupported relation: ", static_cast<int>(relation));

    return kNullEntity;
}

EntityId
EntityManager::setRelation(Relation relation, EntityId subjectEntity, EntityId targetEntity) {
    if (relation == CHILD_OF) {

        if (!m_parentRelations.contains(entityBits(targetEntity))) {
            registerRelation(CHILD_OF, targetEntity);
        }
        insertRelation(subjectEntity, m_parentRelations[entityBits(targetEntity)]);
        return subjectEntity;
    }

    IR_ASSERT(false, "Unsupported relation: ", static_cast<int>(relation));
    return kNullEntity;
}

void EntityManager::insertRelation(EntityId entity, RelationId relation) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    EntityRecord &record = getRecord(entity);
    Archetype newArchetype = record.archetypeNode->type_;
    newArchetype.insert(relation);
    ArchetypeNode *toNode = m_archetypeGraph.findCreateArchetypeNode(newArchetype);
    moveEntityByArchetype(record, record.archetypeNode->type_, record.archetypeNode, toNode);
    IRE_LOG_DEBUG("Moved entity to new archetype with relation {}", relation);
}

smart_ComponentData EntityManager::createComponentDataVector(ComponentId component) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    return m_pureComponentVectors[component]->cloneEmpty();
}

ComponentId
EntityManager::registerComponentImpl(const std::string &typeName, smart_ComponentData impl) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    ComponentId componentId = createEntity();
    m_pureComponentTypes.insert({typeName, componentId});
    m_pureComponentVectors.emplace(componentId, std::move(impl));
    return componentId;
}

ComponentId
EntityManager::registerComponentDynamic(const std::string &typeName, smart_ComponentData impl) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    if (m_pureComponentTypes.contains(typeName)) {
        IRE_LOG_WARN("registerComponentDynamic skipped: type {} already registered", typeName);
        return kNullComponent;
    }
    ComponentId componentId = registerComponentImpl(typeName, std::move(impl));
    IRE_LOG_INFO(
        "Registered dynamic component type={} with id={}",
        typeName,
        static_cast<int>(componentId)
    );
    return componentId;
}

void EntityManager::addComponentByIdImpl(EntityRecord &record, ComponentId componentType) {
    ArchetypeNode *fromNode = record.archetypeNode;
    Archetype type = fromNode->type_;
    type.insert(componentType);
    ArchetypeNode *toNode = m_archetypeGraph.findCreateArchetypeNode(type);
    moveEntityByArchetype(record, fromNode->type_, fromNode, toNode);
    bool ok = toNode->components_[componentType]->appendDefaultRow();
    IR_ASSERT(
        ok,
        "addComponentDynamic: impl for componentId={} does not support appendDefaultRow — "
        "use the templated setComponent<T>(entity, value) path for C++-typed components",
        static_cast<int>(componentType)
    );
    IR_ASSERT(
        toNode->components_[componentType]->size() == toNode->length_,
        "addComponentDynamic: column out of sync with archetype row count"
    );
}

void EntityManager::addComponentDynamic(EntityId entity, ComponentId componentType) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    EntityRecord &record = getRecord(entity);
    if (record.archetypeNode->type_.contains(componentType)) {
        return;
    }
    addComponentByIdImpl(record, componentType);
}

std::pair<IComponentData *, int>
EntityManager::getComponentDataAndRow(EntityId entity, ComponentId componentType) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    if (!entityExists(entity)) {
        return {nullptr, -1};
    }
    EntityRecord &record = getRecord(entity);
    ArchetypeNode *node = record.archetypeNode;
    auto it = node->components_.find(componentType);
    if (it == node->components_.end()) {
        return {nullptr, -1};
    }
    return {it->second.get(), record.row};
}

bool EntityManager::hasComponent(EntityId entity, ComponentId componentType) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    if (!entityExists(entity))
        return false;
    return getRecord(entity).archetypeNode->type_.contains(componentType);
}

void EntityManager::pushCopyData(
    IComponentData *fromStructure, unsigned int fromIndex, IComponentData *toStructure
) {
    fromStructure->pushCopyData(toStructure, fromIndex);
}

int EntityManager::moveEntityByArchetype(
    EntityRecord &record, const Archetype &type, ArchetypeNode *fromNode, ArchetypeNode *toNode
) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);

    if (fromNode == toNode) {
        return record.row;
    }

    IR_ASSERT(
        std::includes(fromNode->type_.begin(), fromNode->type_.end(), type.begin(), type.end()),
        "Entity move type is not a subset of fromNode type"
    );
    IR_ASSERT(
        std::includes(toNode->type_.begin(), toNode->type_.end(), type.begin(), type.end()),
        "Entity move type is not a subset of to node type"
    );
    // EntityRecord& record = getRecord(entity);
    for (auto itr = type.begin(); itr != type.end(); itr++) {
        handleComponentMove(*itr, fromNode, toNode, record.row);
    }
    toNode->entities_.push_back(fromNode->entities_.at(record.row));
    updateBackEntityPosition(fromNode, record.row);
    // updateRecord
    record.archetypeNode = toNode;
    record.row = toNode->length_;
    toNode->length_++;
    fromNode->length_--;
    return record.row;
}

void EntityManager::removeComponentById(
    EntityRecord &record,
    EntityId entity,
    ComponentId componentType,
    ArchetypeNode *fromNode,
    ArchetypeNode *toNode
) {
    unsigned int row = record.row;

    moveEntityByArchetype(record, toNode->type_, fromNode, toNode);

    fromNode->components_[componentType]->removeDataAndPack(row);

    IRE_LOG_DEBUG(
        "Removed component type={} from entity={} (new row={})",
        componentType,
        entity,
        record.row
    );
}

// TODO: Be able to move things in batches (when performance requires it)
void EntityManager::handleComponentMove(
    const ComponentId component,
    ArchetypeNode *fromNode,
    ArchetypeNode *toNode,
    const unsigned int row
) {
    if (!isPureComponent(component)) {
        // IR_ASSERT(false, "non pure components not supported rn");
        return; // Like for relation components with no data, tags maybe, etc
    }
    fromNode->components_.at(component)->moveDataAndPack(
        toNode->components_.at(component).get(),
        row
    );
}

void EntityManager::removeEntityFromArchetypeNode(ArchetypeNode *node, unsigned int index) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    for (auto itr = node->type_.begin(); itr != node->type_.end(); itr++) {
        handleComponentRemove(*itr, node, index);
    }
    updateBackEntityPosition(node, index);
    node->length_--;
}

void EntityManager::handleComponentRemove(
    const ComponentId component, ArchetypeNode *node, const unsigned int row
) {

    if (!isPureComponent(component)) {
        return;
        IR_ASSERT(false, "non pure components not supported rn");
    }
    node->components_[component]->removeDataAndPack(row);
}

void EntityManager::updateBackEntityPosition(ArchetypeNode *node, unsigned int newPos) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    EntityId backEntity = node->entities_.back();
    EntityRecord &backRecord = getRecord(backEntity);
    backRecord.row = newPos;
    node->entities_[newPos] = node->entities_.back();
    node->entities_.pop_back();
    IRE_LOG_DEBUG("Entity={} moved to row={}", backEntity, newPos);
}

void EntityManager::updateRecord(EntityId entity, ArchetypeNode *node, unsigned int row) {
    EntityRecord &record = getRecord(entity);
    record.archetypeNode = node;
    record.row = row;
}

} // namespace IREntity