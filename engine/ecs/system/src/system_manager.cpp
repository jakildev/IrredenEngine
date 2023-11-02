#include <irreden/ir_system.hpp>
#include <irreden/system/system_manager.hpp>
// #include <irreden/command/command_manager.hpp>

namespace IRECS {

    SystemManager::SystemManager()
    :   m_systems{}
    ,   m_systemOrders{}
    {
        for(int i = 0; i < IRSystemType::NUM_SYSTEM_TYPES; i++) {
            m_systemOrders
                [static_cast<IRSystemType>(i)
            ] = std::list<SystemName>{};
        }
        g_systemManager = this;
        IRProfile::engLogInfo("SystemManager initalized");
    };

    void SystemManager::executeSystem(
        std::unique_ptr<SystemVirtual> &system
    )
    {
        std::stringstream ss;
        ss << "SystemBase::tick " << static_cast<int>(system->getSystemName());
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
