#ifndef IR_ENTITY_H
#define IR_ENTITY_H

#include <irreden/ir_math.hpp>

#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/entity/entity_manager.hpp>

namespace IRECS {
    // Gets created by ir_world and set here.
    // Might just make managers static classes in the future, but then
    // cleanup order becomes a bit more ambiguous. Will have to write
    // cleanup functions, etc.
    extern EntityManager* g_entityManager;
    EntityManager& getEntityManager();

    smart_ComponentData createComponentData(ComponentId type);

    std::string makeComponentString(const Archetype& type);

    template <typename... Components>
    Archetype getArchetype() {
        return getEntityManager().getArchetype<Components...>();
    }

    template <typename Component>
    ComponentId getComponentType() {
        return getEntityManager().getComponentType<Component>();
    }

    template <typename Component>
    std::vector<Component>& getComponentData(ArchetypeNode *node) {
        return getEntityManager().getComponentData<Component>(node);
    }

    std::vector<ArchetypeNode*> queryArchetypeNodesSimple(
        const Archetype includeComponents,
        const Archetype excludeComponents = Archetype{}
    );

    bool isPureComponent(ComponentId component);

    template <
        typename... Components
    >
    EntityId createEntity(
        const Components&... components
    )
    {
        return getEntityManager().createEntity(
            components...
        );

    }

    EntityId setParent(EntityId child, EntityId parent);

    // Returns the first EntityId of the batch
    // Needs to guarentee that entities are ajacent for
    // voxel scenes to work
    template <typename... Components>
    std::vector<EntityId> createEntityBatch(
        int count,
        const Components&... components
    )
    {
        std::vector<EntityId> res;
        for (int i = 0; i < count; i++) {
            res.push_back(
                createEntity(
                    components...
                )
            );
        }
        return res;
    }

    template <typename... Functions>
    std::vector<EntityId> createEntitiesBatchWithFunctions(
        IRMath::ivec3 numEntities,
        Functions... functions
    )
    {
        std::vector<EntityId> res;
        for(int i = 0; i < numEntities.x; i++) {
            for(int j = 0; j < numEntities.y; j++) {
                for(int k = 0; k < numEntities.z; k++) {
                    res.push_back(
                        createEntity(
                            functions(
                                IRMath::ivec3{i, j, k}
                            )...
                        )
                    );
                }
            }
        }
        return res;
    }

    template <typename Component>
    Component& getComponent(EntityId entity) {
        return getEntityManager().getComponent<Component>(entity);
    }

    template <typename Component>
    Component& setComponent(EntityId entity, Component component) {
        return getEntityManager().setComponent(entity, component);
    }


} // namespace IRECS

#endif /* IR_ENTITY_H */
