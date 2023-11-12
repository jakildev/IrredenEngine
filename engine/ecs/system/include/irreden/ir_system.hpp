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

    // IRECS::createSystem();

} // namespace System

#endif /* IR_SYSTEM_H */
