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

    // IRECS::createSystem();

} // namespace System

#endif /* IR_SYSTEM_H */
