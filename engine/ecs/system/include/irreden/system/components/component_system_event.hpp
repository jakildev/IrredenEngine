#ifndef COMPONENT_SYSTEM_EVENT_H
#define COMPONENT_SYSTEM_EVENT_H

#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/system/ir_system_types.hpp>

#include <functional>
#include <vector>


namespace IRComponents {

    template <IRECS::SystemEvent event>
    struct C_SystemEvent;

    template <>
    struct C_SystemEvent<IRECS::BEGIN_TICK> {
        std::function<void()> functionBeginTick_ ;

        // std::vector<std::function<void(IRECS::ArchetypeNode*)>> tickFunctions_ ;

        C_SystemEvent(const std::function<void()>& function)
        :   functionBeginTick_(function)
        {

        }
        C_SystemEvent()
        :   functionBeginTick_()
        {

        }
    };

    template <>
    struct C_SystemEvent<IRECS::TICK> {
        std::function<void(IRECS::ArchetypeNode*)> functionTick_;
        IRECS::Archetype archetype_;

        C_SystemEvent(
            const std::function<void(IRECS::ArchetypeNode*)>& tickFunctions,
            const IRECS::Archetype& archetype
        )
        :   functionTick_(tickFunctions)
        ,   archetype_(archetype)
        {

        }

        C_SystemEvent()
        :   functionTick_()
        {

        }
    };

    template <>
    struct C_SystemEvent<IRECS::END_TICK> {
        std::function<void()> functionEndTick_ ;

        C_SystemEvent(const std::function<void()>& function)
        :   functionEndTick_(function)
        {

        }
        C_SystemEvent()
        :   functionEndTick_()
        {

        }
    };

}

#endif /* COMPONENT_SYSTEM_EVENT_H */
