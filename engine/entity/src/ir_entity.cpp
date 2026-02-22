#include <irreden/ir_entity.hpp>

namespace IREntity {

EntityManager *g_entityManager = nullptr;
EntityManager &getEntityManager() {
    IR_ASSERT(g_entityManager != nullptr, "EntityManager not initialized");
    return *g_entityManager;
}

void destroyEntity(EntityId entity) {
    getEntityManager().markEntityForDeletion(entity);
}

void destroyAllEntities() {
    getEntityManager().destroyAllEntities();
}

smart_ComponentData createComponentData(ComponentId type) {
    return getEntityManager().createComponentDataVector(type);
}

std::string makeComponentString(const Archetype &type) {
    return makeComponentStringInternal(type);
}

std::vector<ArchetypeNode *> queryArchetypeNodesSimple(const Archetype &includeComponents,
                                                       const Archetype &excludeComponents) {
    return getEntityManager().getArchetypeGraph()->queryArchetypeNodesSimple(includeComponents,
                                                                             excludeComponents);
}

std::vector<ArchetypeNode *> queryArchetypeNodesRelational(const Relation relation,
                                                           const Archetype &includeComponents,
                                                           const Archetype &excludeComponents) {
    return getEntityManager().getArchetypeGraph()->queryArchetypeNodesRelational(
        relation, includeComponents, excludeComponents);
}

bool isPureComponent(ComponentId component) {
    return getEntityManager().isPureComponent(component);
}

bool isChildOfRelation(RelationId relation) {
    return getEntityManager().isChildOfRelation(relation);
}

EntityId setParent(EntityId child, EntityId parent) {
    return getEntityManager().setRelation(CHILD_OF, child, parent);
}

NodeId getParentNodeFromRelation(RelationId relation) {
    return getEntityManager().getParentNodeFromRelation(relation);
}

EntityId getParentEntityFromArchetype(Archetype type) {
    return getEntityManager().getParentEntityFromArchetype(type);
}

void setName(EntityId entity, const std::string &name) {
    getEntityManager().setName(entity, name);
}

EntityId getEntity(const std::string &name) {
    return getEntityManager().getEntityByName(name);
}

EntityRecord getEntityRecord(EntityId entity) {
    return getEntityManager().getRecord(entity);
}

void handleCreateEntityExtraParams(EntityId entity, const CreateEntityExtraParams &params) {
    if (params.relation.first != Relation::NONE) {
        getEntityManager().setRelation(params.relation.first, entity, params.relation.second);
    }
}

EntityId getRelatedEntityFromArchetype(Archetype type, Relation relation) {
    return getEntityManager().getRelatedEntityFromArchetype(type, relation);
}

// bool isRelationCompoenent(ComponentId component) {
//     return getEntityManager().isRelationComponent(component);
// }
} // namespace IREntity