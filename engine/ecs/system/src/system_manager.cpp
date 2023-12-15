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
    :   m_nextSystemId{0}
    {
        g_systemManager = this;
        IRE_LOG_INFO("Created SystemManager");
    }


    void SystemManager::registerPipeline(
        IRTime::Events event,
        std::list<SystemId> pipeline
    ) {
        m_systemPipelinesNew[event] = pipeline;
    }


    void SystemManager::executePipeline(IRTime::Events event) {
        auto& systemOrder = m_systemPipelinesNew[event];
        for(SystemId system : systemOrder) {
            executeSystem(system);
        }
    }

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

}
