#include <irreden/ir_system.hpp>

namespace IRSystem {

SystemManager *g_systemManager = nullptr;

SystemManager &getSystemManager() {
    return *g_systemManager;
}

void registerPipeline(IRTime::Events event, std::list<SystemId> pipeline) {
    getSystemManager().registerPipeline(event, pipeline);
}

void registerPipelineGroups(
    IRTime::Events event, std::vector<std::vector<SystemId>> groups
) {
    getSystemManager().registerPipelineGroups(event, std::move(groups));
}

void validateAllPipelineGroups() {
    getSystemManager().validateAllPipelineGroups();
}

void executePipeline(IRTime::Events event) {
    getSystemManager().executePipeline(event);
}
} // namespace IRSystem