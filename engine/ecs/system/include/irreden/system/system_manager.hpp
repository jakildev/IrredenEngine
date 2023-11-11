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

        template <
            typename... Components,
            typename Function
        >
        int registerUserSystem(
            std::string name,
            Function function
        )
        {
            m_userSystemFunctions[m_nextUserSystemId] =
                std::make_pair(
                    getArchetype<Components...>(),
                    [function](ArchetypeNode* node) {
                        auto componentsTuple = std::make_tuple(
                            std::ref(getComponentData<Components>(node))...
                        );
                        for(int i = 0; i < node->length_; i++) {
                            std::apply([i, &function](auto&&... components) {
                                function(components[i]...);
                            }, componentsTuple);
                        }
                    }
                );

            IRProfile::engLogInfo(
                "Registered new user system {}",
                name,
                m_nextUserSystemId
            );
            m_nextUserSystemId++;
            return m_nextUserSystemId - 1;
        }

        template <IRSystemType systemType>
        void executeGroup() {
            auto& systemOrder = getSystemExecutionOrder<systemType>();
            for(const auto systemName : systemOrder) {
                executeSystem(m_systems[systemName]);
            }
        }

        void executeUserSystem(int systemId) {
            IR_PROFILE_FUNCTION();
            auto& function = m_userSystemFunctions[systemId].second;
            for(auto& node : IRECS::queryArchetypeNodesSimple(
                m_userSystemFunctions[systemId].first,
                Archetype{}
            )) {
                function(node);
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

        // Maybe systems shouldnt provide an API and thus
        // can just be retrived as virtual systems?
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
        // TODO: Unify engine systems and engine systems by assigning each
        // enum to an index value...
        std::unordered_map<SystemName, std::unique_ptr<SystemVirtual>>
            m_systems;
        std::vector<std::unique_ptr<SystemVirtual>>
            m_userSystems;
        std::unordered_map<IRSystemType, std::list<SystemName>>
            m_systemOrders;
        std::unordered_map<
            int,
            std::pair<Archetype, std::function<void(ArchetypeNode*)>>
        > m_userSystemFunctions;
        SystemId m_nextUserSystemId; // TODO: Use this with registering engine systems too.
        // That way proper pipelines can be made.
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
