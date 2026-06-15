#include <irreden/ir_system.hpp>

namespace IRSystem {

SystemManager *g_systemManager = nullptr;

SystemManager &getSystemManager() {
    return *g_systemManager;
}

void registerPipeline(IRTime::Events event, std::list<SystemId> pipeline) {
    getSystemManager().registerPipeline(event, pipeline);
}

void registerPipelineGroups(IRTime::Events event, std::vector<std::vector<SystemId>> groups) {
    getSystemManager().registerPipelineGroups(event, std::move(groups));
}

void appendToPipeline(IRTime::Events event, SystemId system) {
    getSystemManager().appendToPipeline(event, system);
}

void insertIntoPipelineBefore(IRTime::Events event, SystemId system, SystemId anchor) {
    getSystemManager().insertIntoPipelineBefore(event, system, anchor);
}

void insertIntoPipelineAfter(IRTime::Events event, SystemId system, SystemId anchor) {
    getSystemManager().insertIntoPipelineAfter(event, system, anchor);
}

void validateAllPipelineGroups() {
    getSystemManager().validateAllPipelineGroups();
}

void clearPipeline(IRTime::Events event) {
    getSystemManager().clearPipeline(event);
}

void executePipeline(IRTime::Events event) {
    getSystemManager().executePipeline(event);
}
} // namespace IRSystem