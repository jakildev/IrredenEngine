#ifndef COMPONENT_SYSTEM_EVENT_H
#define COMPONENT_SYSTEM_EVENT_H

#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/system/ir_system_types.hpp>

#include <functional>
#include <vector>

namespace IRComponents {

template <IRSystem::SystemEvent event> struct C_SystemEvent;

template <> struct C_SystemEvent<IRSystem::BEGIN_TICK> {
    std::function<void()> functionBeginTick_;

    // std::vector<std::function<void(IRECS::ArchetypeNode*)>> tickFunctions_ ;

    C_SystemEvent(const std::function<void()> &function) : functionBeginTick_(function) {}
    C_SystemEvent() : functionBeginTick_() {}
};

template <> struct C_SystemEvent<IRSystem::TICK> {
    std::function<void(IREntity::ArchetypeNode *)> functionTick_;
    IREntity::Archetype archetype_;

    C_SystemEvent(const std::function<void(IREntity::ArchetypeNode *)> &tickFunctions,
                  const IREntity::Archetype &archetype)
        : functionTick_(tickFunctions), archetype_(archetype) {}

    C_SystemEvent() : functionTick_() {}
};

template <> struct C_SystemEvent<IRSystem::END_TICK> {
    std::function<void()> functionEndTick_;

    C_SystemEvent(const std::function<void()> &function) : functionEndTick_(function) {}
    C_SystemEvent() : functionEndTick_() {}
};

template <> struct C_SystemEvent<IRSystem::RELATION_TICK> {
    std::function<void(IREntity::EntityRecord)> functionRelationTick_;
    IREntity::Archetype archetype_;

    C_SystemEvent(const std::function<void(IREntity::EntityRecord)> &relationTickFunction)
        : functionRelationTick_(relationTickFunction) {}

    C_SystemEvent() : functionRelationTick_() {}
};

} // namespace IRComponents

#endif /* COMPONENT_SYSTEM_EVENT_H */
