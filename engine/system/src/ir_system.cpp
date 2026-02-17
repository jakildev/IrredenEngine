#include <irreden/ir_system.hpp>

namespace IRSystem {

SystemManager *g_systemManager = nullptr;

SystemManager &getSystemManager() {
    return *g_systemManager;
}

void registerPipeline(IRTime::Events event, std::list<SystemId> pipeline) {
    getSystemManager().registerPipeline(event, pipeline);
}

void executePipeline(IRTime::Events event) {
    getSystemManager().executePipeline(event);
}
} // namespace IRSystem