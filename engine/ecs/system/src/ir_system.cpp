#include <irreden/ir_system.hpp>

namespace IRECS {

    SystemManager* g_systemManager = nullptr;

    SystemManager& getSystemManager() {
        return *g_systemManager;
    }

    void executePipeline(SystemTypes systemType) {
        getSystemManager().executePipeline(systemType);
    }
} // namespace IRECS