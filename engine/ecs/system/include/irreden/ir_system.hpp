#ifndef IR_SYSTEM_H
#define IR_SYSTEM_H

#include <irreden/system/ir_system_types.hpp>
#include <irreden/system/system_manager.hpp>
#include <irreden/system/system_virtual.hpp>
#include <irreden/system/system_base.hpp>

namespace IRECS {
    extern SystemManager* g_systemManager;
    SystemManager& getSystemManager();

    template <SystemName systemName>
    System<systemName>& getEngineSystem() {
        return getSystemManager().get<systemName>();
    }

    template <
        typename... Components,
        typename Function
    >
    int registerUserSystem(
        std::string name,
        Function function
    )
    {
        return getSystemManager().registerUserSystem<Components...>(
            name,
            function
        );
    }

    template <
        typename... Components,
        typename FunctionTick,
        typename FunctionBeginTick = std::nullptr_t,
        typename FunctionEndTick = std::nullptr_t
    >
    SystemId createSystem(
        std::string name,
        FunctionTick functionTick,
        FunctionBeginTick functionBeginTick = nullptr,
        FunctionEndTick functionEndTick = nullptr,
        Relation relation = Relation::NONE
    )
    {
        return getSystemManager().createSystem<Components...>(
            name,
            functionTick,
            functionBeginTick,
            functionEndTick,
            relation
        );
    }

     template <
        SystemName type,
        typename... Args
    >
    SystemId createSystem(Args&&... args) {
        return System<type>::create(
            args...
        );
    }

    template <
        typename ComponentTag
    >
    void addSystemTag(SystemId system) {
        getSystemManager().addSystemTag<ComponentTag>(system);
    }

    // template <SystemName systemName>
    // SystemId createEngineSystem() {
    //     return getSystemManager().createEngineSystem<systemName>();
    // }


    // IRECS::createSystem();

} // namespace System

#endif /* IR_SYSTEM_H */
