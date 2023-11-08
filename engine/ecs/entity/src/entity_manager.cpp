/*
 * Project: Irreden Engine
 * File: entity_manager.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_profile.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/entity/entity_manager.hpp>

#include <memory>
#include <sstream>

namespace IRECS {

    EntityManager::EntityManager()
        :   m_entityPool{}
        ,   m_entityIndex{}
        ,   m_archetypeGraph{}
        ,   m_pureComponentTypes{}
        ,   m_pureComponentVectors{}
        ,   m_liveEntityCount{0}
        ,   m_entitiesMarkedForDeletion{}

        {
        for (EntityId entity = IR_RESERVED_ENTITIES; entity < IR_MAX_ENTITIES; entity++) {
            m_entityPool.push(entity);
        }
        g_entityManager = this;
        IRProfile::engLogInfo(
            "Created Entity Manager (IR_MAX_ENTITIES={})",
            static_cast<int>(IR_MAX_ENTITIES)
        );
    }

    EntityManager::~EntityManager() {}

    // EntityId EntityManager::createEntity() {
    //     IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
    //     EntityId id = allocateEntity();
    //     addNewEntityToBaseNode(id);
    //     return id;
    // }

    EntityId EntityManager::allocateEntity() {
        IR_ASSERT(m_liveEntityCount < IR_MAX_ENTITIES, "Max entity size reached");
        EntityId id = m_entityPool.front();
        m_entityPool.pop();
        m_entityIndex.emplace(
            id & IR_ENTITY_ID_BITS,
            EntityRecord{nullptr, -1}
        );
        m_liveEntityCount++;

        IRProfile::engLogDebug("Created entity={}", id);
        return id;
    }

    void EntityManager::addNewEntityToBaseNode(EntityId id) {
        ArchetypeNode* node = m_archetypeGraph.getBaseNode();
        m_entityIndex.emplace(
            id & IR_ENTITY_ID_BITS,
            EntityRecord{node, node->length_}
        );
        node->entities_.push_back(id);
        node->length_++;
    }

    void EntityManager::returnEntityToPool(EntityId entity) {
        m_entityIndex.erase(entity & IR_ENTITY_ID_BITS);
        --m_liveEntityCount;
        entity &= IR_ENTITY_ID_BITS;
        m_entityPool.push(entity);
    }

    void EntityManager::addFlags(EntityId entity, EntityId flags) {
        IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
        EntityRecord& record = getRecord(entity);
        // Make sure ID bits are not getting modified
        record.archetypeNode->entities_.at(
            record.row) |= (flags & (~IR_ENTITY_ID_BITS)
        );
        EntityId newId = record.archetypeNode->entities_.at(record.row);
    }

    void EntityManager::markEntityForDeletion(EntityId& entity) {
        m_entitiesMarkedForDeletion.push_back(entity);
        entity |= IR_ENTITY_FLAG_MARKED_FOR_DELETION;
    }

    /* TODO: destroy entities in batch after each frame */
    void EntityManager::destroyEntity(EntityId entity) {
        IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
        EntityRecord& record = getRecord(entity);
        IRProfile::engLogDebug("entity={}, record.row={}", entity, record.row);
        ArchetypeNode* node = record.archetypeNode;
        destroyComponents(entity);
        removeEntityFromArchetypeNode(node, record.row);
        returnEntityToPool(entity);
        IRProfile::engLogDebug(
            "Destroyed entity {}",
            entity & IR_ENTITY_ID_BITS
        );
    }

    void EntityManager::destroyComponents(EntityId entity) {
        IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
        EntityRecord& record = getRecord(entity);
        const Archetype& type = record.archetypeNode->type_;
        for(auto itr = type.begin(); itr != type.end(); itr++) {
            destroyComponent(*itr, record.archetypeNode, record.row);
        }
    }

    void EntityManager::destroyComponent(
        ComponentId component,
        ArchetypeNode* node,
        unsigned int index
    )
    {
        IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
        if(!isPureComponent(component)) {

            IR_ASSERT(false, "non pure components not supported rn");
        }
        node->components_.at(component)->destroy(index);
        return;
    }

    void EntityManager::destroyMarkedEntities() {
        // IRProfile::engLogDebug("")
        for(int i = 0; i < m_entitiesMarkedForDeletion.size(); ++i) {
            this->destroyEntity(m_entitiesMarkedForDeletion.at(i));
        }
        m_entitiesMarkedForDeletion.clear();
    }

    EntityRecord& EntityManager::getRecord(EntityId entity) {
        return m_entityIndex[entity & IR_ENTITY_ID_BITS];
    }

    bool EntityManager::isPureComponent(ComponentId component) {
        IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
        return m_pureComponentVectors.find(component) !=
            m_pureComponentVectors.end();
    }

    // This could look up component in archetype graph and clone from there,
    // thus eliminating the need for the m_pureComponentVectors entirely
    smart_ComponentData EntityManager::createComponentDataVector(
        ComponentId component
    )
    {
        IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
        return m_pureComponentVectors[component]->cloneEmpty();
    }

    void EntityManager::pushCopyData(
        IComponentData* fromStructure,
        unsigned int fromIndex,
        IComponentData* toStructure) {
            fromStructure->pushCopyData(toStructure, fromIndex);
    }

    int EntityManager::moveEntityByArchetype(
        EntityRecord& record,
        const Archetype& type,
        ArchetypeNode* fromNode,
        ArchetypeNode* toNode
    )
    {
        IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);

        if(fromNode == toNode) {
            return record.row;
        }

        IR_ASSERT(
            std::includes(
                fromNode->type_.begin(),
                fromNode->type_.end(),
                type.begin(),
                type.end()),
            "Entity move type is not a subset of fromNode type");
        IR_ASSERT(
            std::includes(
                toNode->type_.begin(),
                toNode->type_.end(),
                type.begin(),
                type.end()),
            "Entity move type is not a subset of toNode type");
        // EntityRecord& record = getRecord(entity);
        for(auto itr = type.begin(); itr != type.end(); itr++) {
            handleComponentMove(
                *itr,
                fromNode,
                toNode,
                record.row);
        }
        toNode->entities_.push_back(
            fromNode->entities_.at(record.row));
        updateBackEntityPosition(fromNode, record.row);
        record.archetypeNode = toNode;
        record.row = toNode->length_;
        toNode->length_++;
        fromNode->length_--;
        return record.row;
    }

    // TODO: Be able to move things in batches (when performance requires it)
    void EntityManager::handleComponentMove(
        const ComponentId component,
        ArchetypeNode* fromNode,
        ArchetypeNode* toNode,
        const unsigned int row
    )
    {
        if(!isPureComponent(component)) {
            IR_ASSERT(false, "non pure components not supported rn");
        }
        fromNode->components_.at(component)->moveDataAndPack(
            toNode->components_.at(component).get(),
            row
        );

    }

    void EntityManager::removeEntityFromArchetypeNode(
        ArchetypeNode* node,
        unsigned int index) {
        IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
        for(auto itr = node->type_.begin(); itr != node->type_.end(); itr++) {
            handleComponentRemove(
                *itr,
                node,
                index);
        }
        updateBackEntityPosition(node, index);
        node->length_--;
    }

    void EntityManager::handleComponentRemove(
        const ComponentId component,
        ArchetypeNode* node,
        const unsigned int row)
    {

        if(!isPureComponent(component)) {
            IR_ASSERT(false, "non pure components not supported rn");
        }
        node->components_[component]->removeDataAndPack(row);
    }

    void EntityManager::updateBackEntityPosition(ArchetypeNode* node, unsigned int newPos) {
        IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
        EntityId backEntity = node->entities_.back();
        EntityRecord& backRecord = getRecord(backEntity);
        backRecord.row = newPos;
        node->entities_[newPos] = node->entities_.back();
        node->entities_.pop_back();
        IRProfile::engLogDebug("Entity={} moved to row={}", backEntity, newPos);
    }

    void EntityManager::updateRecord(EntityId entity, ArchetypeNode* node, unsigned int row) {
        EntityRecord& record = getRecord(entity);
        record.archetypeNode = node;
        record.row = row;
    }

} // namespace IRECS: