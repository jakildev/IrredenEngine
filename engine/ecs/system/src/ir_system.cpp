#include <irreden/ir_system.hpp>

namespace IRECS {

    SystemManager* g_systemManager = nullptr;

    SystemManager& getSystemManager() {
        return *g_systemManager;
    }
} // namespace IRECS