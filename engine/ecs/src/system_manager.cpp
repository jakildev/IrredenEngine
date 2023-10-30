#include <irreden/ecs/system_manager.hpp>
// #include <irreden/command/command_manager.hpp>

namespace IRECS {

    void SystemManager::executeSystem(std::unique_ptr<IRECS::IRSystemVirtual> &system) {
        std::stringstream ss;
        ss << "IRSystemBase::tick " << static_cast<int>(system->getSystemName());
        IRProfile::profileBlock(ss.str().c_str(), IR_PROFILER_COLOR_SYSTEMS);
        // TODO: Resolve system commands elsewhere or remove
        // this concept entirely.
        // global.commandManager_->executeSystemCommands(system->getSystemName());
        // global.commandManager_->executeSystemEntityCommands(system->getSystemName());
        system->beginExecute();
        auto nodes =
            IRECS::getEntityManager().
            getArchetypeGraph()->
            queryArchetypeNodesSimple(system->getArchetype());
        for(auto node : nodes) {
            system->tick(node);
        }
        system->endExecute();
    }

}
