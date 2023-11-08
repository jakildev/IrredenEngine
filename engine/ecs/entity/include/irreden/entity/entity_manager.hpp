/*
 * Project: Irreden Engine
 * File: entity_manager.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>,
 */

#ifndef ENTITY_MANAGER_H
#define ENTITY_MANAGER_H

#include <irreden/ir_profile.hpp>

#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/entity/archetype_node.hpp>
#include <irreden/entity/archetype_graph.hpp>
#include <irreden/entity/archetype.hpp>

#include <queue>
#include <sstream>
#include <algorithm>
#include <initializer_list>
#include <set>

// TODO: a component should be registered with a size so it
// can be copied around generically just as data.

// TODO: When entities are truly like components and can have tick functions
// and such, the world will be much easier!!

// Entities can be grouped in their archetype nodes with
// a ChildOf ____ component, where a child of a particular parent
// is a unique component type.

namespace IRECS {

    struct EntityRecord {
        ArchetypeNode* archetypeNode;
        int row;
    };

    class EntityManager
    {
    public:
        EntityManager();
        ~EntityManager();

        inline Archetype& getEntityArchetype(EntityId e) {
            return getRecord(e).archetypeNode->type_;
        }
        inline ArchetypeNode* findArchetypeNode(const Archetype& type) {
            return m_archetypeGraph.findArchetypeNode(type);
        }
        inline const std::vector<smart_ArchetypeNode>& getArchetypeNodes() {
            return m_archetypeGraph.getArchetypeNodes();
        }
        inline const ArchetypeGraph* getArchetypeGraph() const {
            return &m_archetypeGraph;
        }

        template <typename... Components>
        EntityId createEntity(const Components &...components) {
            IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
            EntityId entity = allocateEntity();
            Archetype archetype = getArchetype<Components...>();
            ArchetypeNode* archetypeNode =
                m_archetypeGraph.findCreateArchetypeNode(archetype);
            int index = insertEntityToNode(
                archetypeNode,
                entity,
                components...
            );
            updateRecord(entity, archetypeNode, index);
            IRProfile::engLogDebug("Created entity={} with archetype={}",
                entity,
                makeComponentStringInternal(archetype).c_str()
            );
            return entity;
        }

        EntityRecord& getRecord(EntityId entity);
        EntityId setFlags(EntityId entity, EntityId flags);
        bool isPureComponent(ComponentId component);
        smart_ComponentData createComponentDataVector(ComponentId component);
        void destroyEntity(EntityId entity);
        void markEntityForDeletion(EntityId& entity);
        void destroyMarkedEntities();

        template <Relation Relation>
        void addRelation(EntityId fromEntity, EntityId toEntity); // TODO

        void setChild(EntityId parent, EntityId child); // TODO

        template <typename Component, typename... Args>
        ComponentId registerComponent(Args&&... args) {
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
            // TODO: Just leave this pure component entity in base node or something
            // No need to have this default component biz i dont think...
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

        template <typename Component>
        ComponentId getComponentType() {
            std::string typeName = typeid(Component).name();
            if(m_pureComponentTypes.find(typeName) == m_pureComponentTypes.end()) {
                registerComponent<Component>();
            }
            return m_pureComponentTypes[typeName];
        }

        template <Relation relation>
        RelationId registerRelation(EntityId relatedEntity) {
            RelationId newRelation = createEntity();
            m_relationTypes[relation][newRelation] = relatedEntity;
            setFlags(newRelation, kEntityFlagIsRelation);
            return newRelation;
        }

        // Set component should return an ComponentId
        // The ComponentId can be looked up to figure out what component it belongs to
        // Therefore, it can be looked up in memory
        template <typename Component>
        Component& setComponent(EntityId entity, const Component& component)  {
            IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
            Component& c = getInsertComponent<Component>(entity);
            c = component;
            return c;
        }

        template <typename Component>
        void insertDefaultComponent(EntityRecord& record) {
            ComponentId componentType = getComponentType<Component>();
            ArchetypeNode* fromNode = record.archetypeNode;
            Archetype type = fromNode->type_;
            type.insert(componentType);
            ArchetypeNode* toNode = m_archetypeGraph.findCreateArchetypeNode(type);

            moveEntityByArchetype(
                record,
                fromNode->type_,
                fromNode,
                toNode);

            const Component& component = getComponent<Component>(componentType);
            int insertedIndex = insertComponent<Component>(
                toNode->components_[componentType].get(),
                component
            );

            IR_ASSERT(insertedIndex == toNode->length_ - 1,
                "Component inserted at unexpected location.");

            IRProfile::engLogDebug("Added default component type={} to entity={}: \n\
                \tnew row={}\n\
                \tnew type={}",
                componentType,
                toNode->entities_[record.row],
                record.row,
                makeComponentStringInternal(type).c_str());
        }

        template <typename Component>
        Component& getInsertComponent(EntityId entity) {
            IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
            EntityRecord& record = getRecord(entity);
            ComponentId componentType = getComponentType<Component>();
            Archetype archetype = record.archetypeNode->type_;

            if(std::find(archetype.begin(), archetype.end(), componentType) ==
                archetype.end()) {
                insertDefaultComponent<Component>(record);
            }
            ArchetypeNode* node = record.archetypeNode;
            IComponentDataImpl<Component> *data =
                castComponentDataPointer<Component>(
                    node->components_[componentType].get()
                );

            return data->dataVector[record.row];
        }

        template <typename... Components>
        void setComponents(EntityId entity, const Components &...components) {
            IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
            /* This solution was found here:
            * https://kubasejdak.com/techniques-of-variadic-templates#non-recursive-argument-evaluation-with-stdinitializer-list
            * Essentially, allows pack expanstion, and the initalizer list ends up full of zeros,
            * and should get optimized away. This is to avoid recursion and the associated overhead
            * of creating extra template specializations.
            */
            std::initializer_list<int>{(setComponent(entity, components), 0)...};
        }

        template <typename... Components>
        Archetype getArchetype() {
            IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
            Archetype res{getComponentType<Components>()...};
            return res;
        }


        template <typename Component>
        void removeComponent(EntityId entity)
        {
            IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
            EntityRecord &record = getRecord(entity);
            unsigned int row = record.row;
            ArchetypeNode *fromNode = record.archetypeNode;
            Archetype type = fromNode->type_;
            ComponentId componentType = getComponentType<Component>();
            if(
                std::find(type.begin(), type.end(), componentType) == type.end())
            {
                return;
            }

            type.erase(componentType);
            ArchetypeNode* toNode = m_archetypeGraph.findCreateArchetypeNode(type);

            moveEntityByArchetype(
                record,
                toNode->type_,
                fromNode,
                toNode
            );

            fromNode->components_[componentType]->removeDataAndPack(row);

            IRProfile::engLogDebug("Removed component type={} from entity={} (new row={})",
                        componentType, entity, record.row);
        }


        template <typename Component>
        Component& getComponent(EntityId entity)
        {
            IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
            const EntityRecord& record = getRecord(entity);
            ArchetypeNode* node = record.archetypeNode;
            Archetype archetype = node->type_;
            ComponentId componentType = getComponentType<Component>();

            IR_ASSERT(
                std::find(archetype.begin(), archetype.end(), componentType) !=
                    archetype.end(),
                "Attempted to retrieve non-existant component from entity"
            );
            IComponentDataImpl<Component> *data =
                castComponentDataPointer<Component>(
                    node->components_[componentType].get()
                );

            return data->dataVector[record.row];
        }


        template <typename Component>
        std::vector<Component>& getComponentData(ArchetypeNode *node)
        {
            IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
            Archetype archetype = node->type_;
            ComponentId componentType = getComponentType<Component>();

            IR_ASSERT(std::find(archetype.begin(), archetype.end(), componentType) != archetype.end(),
                    "Attempted to retrieve non-existant component vector from node");
            IComponentDataImpl<Component> *data =
                castComponentDataPointer<Component>(
                    node->components_[componentType].get()
                );

            return data->dataVector;
        }

        template <typename... Components>
        std::vector<EntityId> createEntitiesBatch(
            const std::vector<Components>&... componentVectors
        )
        {
            IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
            std::vector<EntityId> res;
            size_t sizes[] = {componentVectors.size()...};
            IR_ASSERT(
                std::all_of(
                    std::begin(sizes),
                    std::end(sizes),
                    [&sizes](size_t cur){return cur == sizes[0]; }
                ),
                "Create batch entities with different sized component vectors"
            );
            size_t numEntities = sizes[0];
            for(int i = 0; i < numEntities; i++) {
                res.push_back(allocateEntity());
            }
            Archetype archetype = getArchetype<Components...>();
            ArchetypeNode* toNode =
                m_archetypeGraph.findCreateArchetypeNode(archetype);
            std::vector<int> indices = inserComponentsBatch(
                toNode,
                res,
                componentVectors...
            );
            for(int i = 0; i < numEntities; i++) {
                updateRecord(res[i], toNode, indices[i]);
            }
            return res;
        }


    private:
        std::queue<EntityId> m_entityPool;
        std::unordered_map<EntityId, EntityRecord> m_entityIndex;
        ArchetypeGraph m_archetypeGraph;
        std::unordered_map<std::string, ComponentId> m_pureComponentTypes;
        std::unordered_map<
            Relation,
            std::unordered_map<RelationId, EntityId>
        > m_relationTypes;
        // TODO:
        // std::unordered_map<
        //     ComponentId, ComponentTypeInfo> m_component_type_info;;

        std::unordered_map<ComponentId, smart_ComponentData> m_pureComponentVectors;
        EntityId m_liveEntityCount;
        std::vector<EntityId> m_entitiesMarkedForDeletion;

        template <typename Component, typename... Args>
        int emplaceComponent(IComponentData* dest, Args&&... args) {
            IComponentDataImpl<Component>* d = castComponentDataPointer<Component>(dest);
            d->dataVector.emplace_back(std::forward<Args>(args)...);
            int index = d->size() - 1;
            return index;
        }

        template <typename Component>
        int insertComponent(IComponentData* dest, Component component) {
            IComponentDataImpl<Component>* d = castComponentDataPointer<Component>(dest);
            d->dataVector.push_back(component);
            int index = d->size() - 1;
            return index;
        }

        template <typename... Components>
        int insertEntityToNode(
            ArchetypeNode* toNode,
            EntityId entity,
            const Components &...components
        )
        {
            int newIndex = toNode->length_;

            std::tuple<IComponentDataImpl<Components>*...> dataPointers = {
                castComponentDataPointer<Components>(
                    toNode->components_[getComponentType<Components>()].get()
                )...
            };

            std::apply([&](auto&&... args) {
                (args->dataVector.emplace_back(components), ...);
            }, dataPointers);


            toNode->entities_.push_back(entity);
            toNode->length_++;

            return newIndex;

        }

        template <typename... Components>
        std::vector<int> inserComponentsBatch(
            ArchetypeNode* toNode,
            const std::vector<EntityId>& entityIds,
            const std::vector<Components>&... componentVectors
        )
        {
            std::vector<int> indices;
            std::tuple<IComponentDataImpl<Components>*...> dataPointers = {
                castComponentDataPointer<Components>(
                    toNode->components_[getComponentType<Components>()].get()
                )...
            };
            int numEntities = entityIds.size();
            for(int i = 0; i < numEntities; i++) {
                indices.push_back(toNode->length_);
                toNode->length_++;
                toNode->entities_.push_back(entityIds[i]);
                std::apply([&](auto&&... args) {
                    (args->dataVector.emplace_back(componentVectors[i]), ...);
                }, dataPointers);
            }
            return indices;
        }

        EntityId allocateEntity();
        void addNewEntityToBaseNode(EntityId entity);
        void returnEntityToPool(EntityId entity);
        void pushCopyData(
            IComponentData* fromStructure,
            unsigned int fromIndex,
            IComponentData* toStructure);
        int moveEntityByArchetype(
            EntityRecord& entity,
            const Archetype& type,
            ArchetypeNode* fromNode,
            ArchetypeNode* toNode);
        void handleComponentMove(
            const ComponentId component,
            ArchetypeNode* fromNode,
            ArchetypeNode* toNode,
            const unsigned int row);
        void removeEntityFromArchetypeNode(ArchetypeNode* node, unsigned int index);
        void handleComponentRemove(
            const ComponentId component,
            ArchetypeNode* node,
            const unsigned int row);
        void updateBackEntityPosition(ArchetypeNode *node, unsigned int newPos);
        void destroyComponents(EntityId entity);
        void destroyComponent(
            ComponentId component,
            ArchetypeNode* node,
            unsigned int row
        );
        void updateRecord(
            EntityId entity,
            ArchetypeNode* node,
            unsigned int row
        );

    };

} // namespace IRECS

// #include <irreden/entity/entity_manager.tpp>

#endif /* ENTITY_MANAGER_H */
