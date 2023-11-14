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
            m_systemPipelines[SystemType].push_back(SystemName);

            IRProfile::engLogInfo(
                "Registered new system {}",
                static_cast<int>(SystemName)
            );
        }

        template <
            SystemName SystemName,
            IRSystemType SystemType,
            typename TickFunction,
            typename... Components
        >
        SystemId registerEngineSystemNew(
            TickFunction tickFunction,
            std::unique_ptr<SystemVirtual> system = nullptr
        )
        {
            SystemId systemId = m_nextUserSystemId++;
            m_virtualSystems[systemId] = std::move(system);
            m_userSystemFunctions[systemId] =
                SystemUser{
                    .archetype_ = getArchetype<Components...>(),
                    .function_ = [tickFunction](ArchetypeNode* node) {
                        auto componentsTuple = std::make_tuple(
                            std::ref(getComponentData<Components>(node))...
                        );
                        for(int i = 0; i < node->length_; i++) {
                            std::apply([i, &tickFunction](auto&&... components) {
                                tickFunction(components[i]...);
                            }, componentsTuple);
                        }
                    },
                    .relation_ = Relation::NONE
                };
            m_systemPipelines[SystemType].push_back(SystemName);
            IRProfile::engLogInfo(
                "Registered new system {}",
                static_cast<int>(SystemName)
            );

        }

        template <
            typename... Components,
            typename Function
        >
        SystemId registerUserSystem(
            std::string name,
            Function function
        )
        {
            m_userSystemFunctions[m_nextUserSystemId] =
                SystemUser{
                    .archetype_ = getArchetype<Components...>(),
                    .function_ = [function](ArchetypeNode* node) {
                        auto componentsTuple = std::make_tuple(
                            std::ref(getComponentData<Components>(node))...
                        );
                        for(int i = 0; i < node->length_; i++) {
                            std::apply([i, &function](auto&&... components) {
                                function(components[i]...);
                            }, componentsTuple);
                        }
                    },
                    .relation_ = Relation::NONE
                };
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

        void executeUserSystemAll() {
            IR_PROFILE_FUNCTION();
            for(auto& system : m_userSystemFunctions) {
                executeUserSystem(system.second);
            }
        }

        void executeUserSystem(
            SystemUser& system
        );
        void executeSystemTick(
            SystemVirtual* system,
            ArchetypeNode* node
        );

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
        std::unordered_map<SystemId, std::unique_ptr<SystemVirtual>> m_virtualSystems;
        std::vector<std::unique_ptr<SystemVirtual>>
            m_userSystems;
        std::unordered_map<IRSystemType, std::list<SystemName>>
            m_systemPipelines;
        std::unordered_map<
            SystemId,
            SystemUser
        > m_userSystemFunctions;

        void executeSystem(
            std::unique_ptr<SystemVirtual> &system
        );

        template <IRSystemType systemType>
        const std::list<SystemName>& getSystemExecutionOrder() const {
            return m_systemPipelines.at(systemType);
        }
    };

} // namespace IRECS


#endif /* SYSTEM_MANAGER_H */
