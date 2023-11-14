#include <irreden/ir_system.hpp>
#include <irreden/system/system_manager.hpp>

namespace IRECS {

    SystemManager::SystemManager()
    :   m_systems{}
    ,   m_systemPipelines{}
    ,   m_nextUserSystemId{0}
    {
        for(int i = 0; i < IRSystemType::NUM_SYSTEM_TYPES; i++) {
            m_systemPipelines
                [static_cast<IRSystemType>(i)
            ] = std::list<SystemName>{};
        }
        g_systemManager = this;
        IRProfile::engLogInfo("Created SystemManager");
    };

    void SystemManager::executeSystem(
        std::unique_ptr<SystemVirtual> &system
    )
    {
        std::stringstream ss;
        ss << "SystemBase::tick " << static_cast<int>(system->getSystemName());
        IR_PROFILE_BLOCK(ss.str().c_str(), IR_PROFILER_COLOR_SYSTEMS);
        system->beginExecute();
        std::vector<ArchetypeNode*> nodes;
        if(system->getRelation() == Relation::NONE) {
            nodes = IRECS::queryArchetypeNodesSimple(
                system->getArchetype()
            );
        }
        if(system->getRelation() == Relation::CHILD_OF) {
            nodes = IRECS::queryArchetypeNodesRelational(
                system->getRelation(),
                system->getArchetype()
            );
        }
        for(auto node : nodes) {
            executeSystemTick(system.get(), node);
            // system->tick(node);
        }
        system->endExecute();
    }

    void SystemManager::executeUserSystem(
        SystemUser& system
    )
    {
        IR_PROFILE_FUNCTION();
        auto& function = system.function_;
        for(auto& node : IRECS::queryArchetypeNodesSimple(
            system.archetype_,
            Archetype{}
        )) {
            function(node);
        }
    }

    void SystemManager::executeSystemTick(
        SystemVirtual* system,
        ArchetypeNode* node
    )
    {
        system->tick(node);
    }

}
