#include <irreden/ir_system.hpp>
#include <irreden/system/system_manager.hpp>

#include <irreden/common/components/component_name.hpp>

namespace IRECS {

    SystemManager::SystemManager()
    :   m_systems{}
    ,   m_systemPipelines{}
    ,   m_nextSystemId{0}
    {
        for(int i = 0; i < IRSystemType::NUM_SYSTEM_TYPES; i++) {
            m_systemPipelines
                [static_cast<IRSystemType>(i)
            ] = std::list<SystemName>{};
        }
        g_systemManager = this;
        IRProfile::engLogInfo("Created SystemManager");
    };

    void SystemManager::executeSystem(SystemId system) {
       std::stringstream ss;
        ss << "Execute System: " << m_systemNames[system].name_;
        IR_PROFILE_BLOCK(ss.str().c_str(), IR_PROFILER_COLOR_SYSTEMS);
        m_systemBeginTicks[system].functionBeginTick_();
        std::vector<ArchetypeNode*> nodes;
        if(m_systemRelations[system].relation_ == Relation::NONE) {
            nodes = IRECS::queryArchetypeNodesSimple(
                m_systemTicks[system].archetype_
            );
        }
        if(m_systemRelations[system].relation_ == Relation::CHILD_OF) {
            nodes = IRECS::queryArchetypeNodesRelational(
                m_systemRelations[system].relation_,
                m_systemTicks[system].archetype_
            );
        }
        for(auto node : nodes) {
            m_systemTicks[system].functionTick_(node);
        }
        m_systemEndTicks[system].functionEndTick_();
    }

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
