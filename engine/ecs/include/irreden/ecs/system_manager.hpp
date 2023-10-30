/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\entity\system_manager.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include <cstdint> // uint32_t
#include <unordered_map>
#include <memory>
#include <string>
#include <list>
#include <irreden/ecs/entity_manager.hpp>
#include <irreden/ir_profiling.hpp>
#include <irreden/ir_ecs.hpp>
#include <irreden/ecs/ir_system_virtual.hpp>

namespace IRECS {

class SystemManager {
    public:
        static SystemManager& instance() {
            static SystemManager instance{};
            return instance;
        }
        ~SystemManager() = default;

        template <IRSystemName SystemName, IRSystemType SystemType, typename... Args>
        void registerSystem(Args&&... args)
        {
            auto systemInstance = std::make_unique<IRSystem<SystemName>>(
                std::forward<Args>(args)...
            );
            m_systems.insert(std::make_pair(SystemName, std::move(systemInstance)));
            m_systemOrders[SystemType].push_back(SystemName);

            // TODO: Here is where all events are subscribed to

            IRProfile::engLogInfo(
                "Registered new system {}",
                static_cast<int>(SystemName)
            );
        }

        template <IRSystemType systemType>
        void executeGroup() {
            auto& systemOrder = getSystemExecutionOrder<systemType>();
            for(const auto systemName : systemOrder) {
                executeSystem(m_systems[systemName]);
            }
        }

        template <IREvents Event>
        void executeEvent() {
            if(Event == IREvents::START) {
                for(auto& system : m_systems) {
                    system.second->start();
                }
            }
            if(Event == IREvents::END) {
                for(auto& system : m_systems) {
                    system.second->end();
                }
            }
        }

        template <IRSystemName SystemName>
        const IRSystem<SystemName>* get() const {
            return static_cast<IRSystem<SystemName>*>(m_systems.at(SystemName).get());
        }

        template <IRSystemName SystemName>
        IRSystem<SystemName>* get() {
            return static_cast<IRSystem<SystemName>*>(m_systems.at(SystemName).get());
        }

        // template <
        //     IRSystemName SystemName,
        //     IREvents Event
        // >
        // void subscribeToEvent() {
        //     m_eventSubscriptions[Event].push_back(SystemName);
        // }


    private:
        std::unordered_map<IRSystemName, std::unique_ptr<IRSystemVirtual>>
            m_systems;
        std::unordered_map<IRSystemType, std::list<IRSystemName>>
            m_systemOrders;
        SystemManager()
        :   m_systems{}
        ,   m_systemOrders{}
        {
            for(int i = 0; i < IRSystemType::NUM_SYSTEM_TYPES; i++) {
                m_systemOrders
                    [static_cast<IRSystemType>(i)
                ] = std::list<IRSystemName>{};
            }
        };
        // TODO: This should be in event manager, and like commands,
        // systems, entities, etc can all subscribe to events!
        // std::unordered_map<
        //     IREvent,
        //     std::vector<IRSystemName>
        // > m_eventSubscriptions;

        void executeSystem(std::unique_ptr<IRECS::IRSystemVirtual> &system);

        template <IRSystemType systemType>
        const std::list<IRSystemName>& getSystemExecutionOrder() const {
            return m_systemOrders.at(systemType);
        }
    };

} // namespace IRECS


#endif /* SYSTEM_MANAGER_H */
