/*
 * Project: Irreden Engine
 * File: ir_entity.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_ENTITY_H
#define IR_ENTITY_H

#include <irreden/ir_math.hpp>

#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/entity/prefabs.hpp>

#include <irreden/common/components/component_position_global_3d.hpp>

namespace IREntity {
    // Gets created by ir_world and set here.
    // Might just make managers static classes in the future, but then
    // cleanup order becomes a bit more ambiguous. Will have to write
    // cleanup functions, etc.
    extern EntityManager* g_entityManager;
    EntityManager& getEntityManager();

    smart_ComponentData createComponentData(ComponentId type);
    std::string makeComponentString(const Archetype& type);
    EntityId getRelatedEntityFromArchetype(Archetype type, Relation relation);
    EntityId getParentEntityFromArchetype(Archetype type);
    void setName(EntityId entity, const std::string& name);
    EntityId getEntity(const std::string& name);
    EntityRecord getEntityRecord(EntityId entity);

    template <typename Component>
    using LuaCreateEntityFunction = std::function<Component(IRMath::ivec3)>;

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
        const Archetype& includeComponents,
        const Archetype& excludeComponents = Archetype{}
    );

    std::vector<ArchetypeNode*> queryArchetypeNodesRelational(
        const Relation relation,
        const Archetype& includeComponents,
        const Archetype& excludeComponents = Archetype{}
    );

    bool isPureComponent(ComponentId component);
    bool isChildOfRelation(RelationId relation);
    NodeId getParentNodeFromRelation(RelationId relation);


    template <
        typename... Components
    >
    EntityId createEntity(
        const Components&... components
    )
    {
        return getEntityManager().createEntity(
            IRComponents::C_PositionGlobal3D{},
            components...
        );
    }

    template <
        PrefabTypes type,
        typename... Args
    >
    EntityId createEntity(Args&&... args) {
        return Prefab<type>::create(
            args...
        );
    }

    // template <
    //     typename... Components
    // >
    // EntityId createEntity_Ext(
    //     const Components&... components,
    //     const CreateEntityExtraParams& params
    // )
    // {
    //     EntityId entity = getEntityManager().createEntity(
    //         IRComponents::C_PositionGlobal3D{},
    //         components...
    //     );

    //     if(params.parentEntity != kNullEntity) {
    //         setParent(entity, params.parentEntity);
    //     }
    // }

    // template <
    //     typename... Components
    // >
    // EntityId createEntityChild(
    //     EntityId parent,
    //     const Components&... components
    // )
    // {
    //     return getEntityManager().createEntity(
    //         components...
    //     );
    // }

    EntityId setParent(EntityId child, EntityId parent);
    void destroyEntity(EntityId entity);

    // Returns the first EntityId of the batch
    // Needs to guarentee that entities are ajacent for
    // voxel scenes to work
    // TODO: Consolidate all createEntity... functions into one variable
    // param structure call. Do the same for systems.
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

    void handleCreateEntityExtraParams(
        EntityId entity,
        const CreateEntityExtraParams& params
    );


    // TODO: Pack vectors and send to entityManager all at once
    // TODO: Consolidate with Ext version
    template <typename... Functions>
    std::vector<EntityId> createEntityBatchWithFunctions(
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

    // TODO: Pack vectors and send to entityManager all at once
    template <typename... Functions>
    std::vector<EntityId> createEntityBatchWithFunctions_Ext(
        IRMath::ivec3 numEntities,
        const CreateEntityExtraParams& params,
        Functions... functions
    )
    {
        const IRMath::vec3 center = vec3(numEntities) / vec3(2);
        IREntity::CreateEntityCallbackParams callbackParams{
            IRMath::ivec3{0, 0, 0},
            center
        };
        std::vector<EntityId> res;
        for(int i = 0; i < numEntities.x; i++) {
            for(int j = 0; j < numEntities.y; j++) {
                for(int k = 0; k < numEntities.z; k++) {
                    callbackParams.index = IRMath::ivec3{i, j, k};
                    res.push_back(
                        createEntity(
                            functions(
                                callbackParams
                            )...
                        )
                    );
                    handleCreateEntityExtraParams(res.back(), params);
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
    Component& getComponent(const std::string& name) {
        return getEntityManager().getComponent<Component>(
            getEntity(name)
        );
    }

    template <typename Component>
    std::optional<Component*> getComponentOptional(EntityId entity) {
        return getEntityManager().getComponentOptional<Component>(entity);
    }

    template <typename Component>
    Component& setComponent(EntityId entity, Component component) {
        return getEntityManager().setComponent(entity, component);
    }


    template <typename Component>
    void removeComponent(EntityId entity) {
        getEntityManager().removeComponent<Component>(entity);
    }

} // namespace IREntity

#endif /* IR_ENTITY_H */
