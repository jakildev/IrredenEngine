#include <irreden/ir_entity.hpp>

namespace IRECS {

    EntityManager* g_entityManager = nullptr;
    EntityManager& getEntityManager() {
        IR_ASSERT(
            g_entityManager != nullptr,
            "EntityManager not initialized"
        );
        return *g_entityManager;
    }

    smart_ComponentData createComponentData(ComponentId type) {
        return getEntityManager().createComponentDataVector(type);
    }

    std::string makeComponentString(const Archetype& type) {
        return makeComponentStringInternal(type);
    }

    std::vector<ArchetypeNode*> queryArchetypeNodesSimple(
        const Archetype includeComponents,
        const Archetype excludeComponents
    ) {
        return
            getEntityManager().
            getArchetypeGraph()->
            queryArchetypeNodesSimple(
                includeComponents,
                excludeComponents
        );
    }

    bool isPureComponent(ComponentId component) {
        return getEntityManager().isPureComponent(component);
    }

    EntityId setParent(EntityId child, EntityId parent) {
        return getEntityManager().setRelation(CHILD_OF, child, parent);
    }

    // bool isRelationCompoenent(ComponentId component) {
    //     return getEntityManager().isRelationComponent(component);
    // }
}