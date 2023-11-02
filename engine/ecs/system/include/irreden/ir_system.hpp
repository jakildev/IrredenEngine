#ifndef IR_SYSTEM_H
#define IR_SYSTEM_H

#include <irreden/system/system_manager.hpp>

namespace IRECS {
    extern SystemManager* g_systemManager;
    SystemManager& getSystemManager();

    template <SystemName systemName>
    System<systemName>& getSystem() {
        return getSystemManager().get<systemName>();
    }

} // namespace System

#endif /* IR_SYSTEM_H */
