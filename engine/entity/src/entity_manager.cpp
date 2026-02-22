#include <irreden/ir_profile.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/entity/entity_manager.hpp>

#include <memory>
#include <sstream>

namespace IREntity {

EntityManager::EntityManager()
    : m_entityPool{}
    , m_entityIndex{}
    , m_archetypeGraph{}
    , m_pureComponentTypes{}
    , m_pureComponentVectors{}
    , m_liveEntityCount{0}
    , m_entitiesMarkedForDeletion{} {
    for (EntityId entity = IR_RESERVED_ENTITIES; entity < IR_MAX_ENTITIES; entity++) {
        m_entityPool.push(entity);
    }
    g_entityManager = this;
    IRE_LOG_INFO("Created EntityManager (IR_MAX_ENTITIES={})", static_cast<int>(IR_MAX_ENTITIES));
}

EntityManager::~EntityManager() {}

EntityId EntityManager::allocateEntity() {
    IR_ASSERT(m_liveEntityCount < IR_MAX_ENTITIES, "Max entity size reached");
    EntityId id = m_entityPool.front();
    m_entityPool.pop();
    m_entityIndex.emplace(id & IR_ENTITY_ID_BITS, EntityRecord{nullptr, -1});
    m_liveEntityCount++;

    IRE_LOG_DEBUG("Created entity={}", id);
    return id;
}

void EntityManager::addNewEntityToBaseNode(EntityId id) {
    ArchetypeNode *node = m_archetypeGraph.getBaseNode();
    m_entityIndex.emplace(id & IR_ENTITY_ID_BITS, EntityRecord{node, node->length_});
    node->entities_.push_back(id);
    node->length_++;
}

void EntityManager::returnEntityToPool(EntityId entity) {
    m_entityIndex.erase(entity & IR_ENTITY_ID_BITS);
    --m_liveEntityCount;
    entity &= IR_ENTITY_ID_BITS;
    m_entityPool.push(entity);
}

EntityId EntityManager::setFlags(EntityId entity, EntityId flags) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    EntityRecord &record = getRecord(entity);
    // Make sure ID bits are not getting modified
    record.archetypeNode->entities_.at(record.row) |= (flags & (~IR_ENTITY_ID_BITS));
    return record.archetypeNode->entities_.at(record.row);
}

void EntityManager::markEntityForDeletion(EntityId &entity) {
    m_entitiesMarkedForDeletion.push_back(entity);
    entity |= IR_ENTITY_FLAG_MARKED_FOR_DELETION;
}

/* TODO: destroy entities in batch after each frame */
void EntityManager::destroyEntity(EntityId entity) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    EntityRecord &record = getRecord(entity);
    IRE_LOG_DEBUG("entity={}, record.row={}", entity, record.row);
    ArchetypeNode *node = record.archetypeNode;
    destroyComponents(entity);
    removeEntityFromArchetypeNode(node, record.row);
    returnEntityToPool(entity);
    IRE_LOG_DEBUG("Destroyed entity {}", entity & IR_ENTITY_ID_BITS);
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
    for (int i = 0; i < m_entitiesMarkedForDeletion.size(); ++i) {
        this->destroyEntity(m_entitiesMarkedForDeletion.at(i));
    }
    m_entitiesMarkedForDeletion.clear();
}

void EntityManager::destroyAllEntities() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);

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

EntityId EntityManager::getParentEntityFromArchetype(Archetype type) {
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