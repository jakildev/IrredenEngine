#include <irreden/ir_entity.hpp>

namespace IRECS {

    EntityManager* g_entityManager = nullptr;
    EntityManager& getEntityManager() {
        IR_ENG_ASSERT(
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
}