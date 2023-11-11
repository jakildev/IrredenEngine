/*
 * Project: Irreden Engine
 * File: system_manager.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include <irreden/ir_profile.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/system/ir_system_types.hpp>
#include <irreden/system/system_virtual.hpp>

#include <cstdint> // uint32_t
#include <unordered_map>
#include <memory>
#include <string>
#include <list>

namespace IRECS {

    class SystemManager {
    public:
        SystemManager();
        ~SystemManager() = default;

        template <
            SystemName SystemName,
            IRSystemType SystemType,
            typename... Args
        >
        void registerEngineSystem(Args&&... args)
        {
            auto systemInstance = std::make_unique<System<SystemName>>(
                std::forward<Args>(args)...
            );
            m_systems.insert(std::make_pair(SystemName, std::move(systemInstance)));
            m_systemOrders[SystemType].push_back(SystemName);

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

        template <IRTime::Events Event>
        void executeEvent() {
            if(Event == IRTime::Events::START) {
                for(auto& system : m_systems) {
                    system.second->start();
                }
            }
            if(Event == IRTime::Events::END) {
                for(auto& system : m_systems) {
                    system.second->end();
                }
            }
        }

        template <SystemName SystemName>
        const System<SystemName>& get() const {
            return *static_cast<System<SystemName>*>(m_systems.at(SystemName).get());
        }

        template <SystemName SystemName>
        System<SystemName>& get() {
            return *static_cast<System<SystemName>*>(m_systems.at(SystemName).get());
        }

        // template <
        //     SystemName SystemName,
        //     IREvents Event
        // >
        // void subscribeToEvent() {
        //     m_eventSubscriptions[Event].push_back(SystemName);
        // }


    private:
        std::unordered_map<SystemName, std::unique_ptr<SystemVirtual>>
            m_systems;
        std::unordered_map<IRSystemType, std::list<SystemName>>
            m_systemOrders;
        // TODO: This should be in event manager, and like commands,
        // systems, entities, etc can all subscribe to events!
        // std::unordered_map<
        //     IREvent,
        //     std::vector<SystemName>
        // > m_eventSubscriptions;

        void executeSystem(
            std::unique_ptr<SystemVirtual> &system
        );

        template <IRSystemType systemType>
        const std::list<SystemName>& getSystemExecutionOrder() const {
            return m_systemOrders.at(systemType);
        }
    };

} // namespace IRECS


#endif /* SYSTEM_MANAGER_H */
