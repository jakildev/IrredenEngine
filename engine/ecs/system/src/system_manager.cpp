/*
 * Project: Irreden Engine
 * File: system_manager.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_system.hpp>
#include <irreden/system/system_manager.hpp>

#include <irreden/common/components/component_name.hpp>

namespace IRECS {

    SystemManager::SystemManager()
    :   m_systems{}
    ,   m_systemPipelines{}
    ,   m_nextSystemId{0}
    {
        for(int i = 0; i < SystemTypes::NUM_SYSTEM_TYPES; i++) {
            m_systemPipelines
                [static_cast<SystemTypes>(i)
            ] = std::list<SystemName>{};
        }
        g_systemManager = this;
        IRProfile::engLogInfo("Created SystemManager");
    };

    void SystemManager::executeSystem(SystemId system) {
       std::stringstream ss;
        ss << "Execute System: " << m_systemNames[system].name_;
        IR_PROFILE_BLOCK(ss.str().c_str(), IR_PROFILER_COLOR_SYSTEMS);
        m_beginTicks[system].functionBeginTick_();
        std::vector<ArchetypeNode*> nodes;
        if(m_relations[system].relation_ == Relation::NONE) {
            nodes = IRECS::queryArchetypeNodesSimple(
                m_ticks[system].archetype_
            );
        }
        if(m_relations[system].relation_ == Relation::CHILD_OF) {
            nodes = IRECS::queryArchetypeNodesRelational(
                m_relations[system].relation_,
                m_ticks[system].archetype_
            );
        }
        for(auto node : nodes) {
            m_ticks[system].functionTick_(node);
        }
        m_endTicks[system].functionEndTick_();
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

    void SystemManager::executeSystemTick(
        SystemVirtual* system,
        ArchetypeNode* node
    )
    {
        system->tick(node);
    }

}
