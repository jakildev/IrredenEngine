/*
 * Project: Irreden Engine
 * File: entity_manager.tpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

namespace IRECS {

    template <typename Component, typename... Args>
    ComponentId EntityManager::registerComponent(Args&&... args) {
        IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
        std::string typeName = typeid(Component).name();
        IR_ASSERT(
            m_pureComponentTypes.find(typeName) == m_pureComponentTypes.end(),
            "Regestering the same component twice"
        );
        ComponentId componentId = createEntity();
        m_pureComponentTypes.insert({typeName, componentId});
        m_pureComponentVectors.emplace(
            componentId,
            std::make_unique<IComponentDataImpl<Component>>()
        );
        Archetype archetype = {componentId};
        ArchetypeNode* toNode =
            m_archetypeGraph.findCreateArchetypeNode(archetype);
        EntityRecord& record = getRecord(componentId);
        ArchetypeNode* fromNode = record.archetypeNode;

        moveEntityByArchetype(
            record,
            fromNode->type_,
            fromNode,
            toNode
        );

        int insertedIndex = emplaceComponent<Component>(
            toNode->components_[componentId].get(),
            std::forward<Args>(args)...
        );
        IR_ASSERT(insertedIndex == toNode->length_ - 1,
            "Component inserted at unexpected location."
        );
        IRProfile::engLogInfo("Regestered component type={}, sizeof={} with id={}",
            typeName,
            sizeof(Component),
            static_cast<int>(componentId)
        );
        return componentId;
    }

        template <IRRelationType Relation>
        void addRelation(EntityId fromEntity, EntityId toEntity) {

        }


}