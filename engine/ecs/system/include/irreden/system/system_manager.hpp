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

#include <irreden/common/components/component_name.hpp>
#include <irreden/system/components/component_system_event.hpp>
#include <irreden/system/components/component_system_relation.hpp>

#include <cstdint> // uint32_t
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <list>

using namespace IRComponents;

namespace IRECS {

    class SystemManager {
    public:
        SystemManager();
        ~SystemManager() = default;

        // there should be one create system, and everything should just use it!
        template <
            typename... Components,
            typename FunctionTick,
            typename FunctionBeginTick = std::nullptr_t,
            typename FunctionEndTick = std::nullptr_t
        >
        SystemId createSystem(
            std::string name,
            FunctionTick functionTick,
            FunctionBeginTick functionBeginTick = nullptr,
            FunctionEndTick functionEndTick = nullptr,
            Relation relation = Relation::NONE
        )
        {
            m_systemNames.emplace_back(C_Name{name});
            if constexpr (!std::is_same_v<FunctionBeginTick, std::nullptr_t>) {
                m_beginTicks.emplace_back(
                    C_SystemEvent<BEGIN_TICK>{[functionBeginTick]() {
                        functionBeginTick();
                    }}
                );
            }
            else {
                m_beginTicks.emplace_back(
                    C_SystemEvent<BEGIN_TICK>{[](){ return; }}
                );
            }
            m_ticks.emplace_back(
                C_SystemEvent<TICK>{
                    [functionTick](ArchetypeNode* node) {
                        auto componentsTuple = std::make_tuple(
                            std::ref(getComponentData<Components>(node))...
                        );
                        for(int i = 0; i < node->length_; i++) {
                            std::apply([i, &functionTick](auto&&... components) {
                                functionTick(components[i]...);
                            }, componentsTuple);
                        }
                    },
                    getArchetype<Components...>()
                }
            );
            if constexpr (!std::is_same_v<FunctionEndTick, std::nullptr_t>) {
                m_endTicks.emplace_back(
                    C_SystemEvent<END_TICK>{[functionEndTick]() {
                        functionEndTick();
                    }}
                );
            }
            else {
                m_endTicks.emplace_back(
                    C_SystemEvent<END_TICK>{[](){ return; }}
                );
            }
            m_relations.emplace_back(
                C_SystemRelation{relation}
            );
            return m_nextSystemId++;
        }

        // TODO: return an id, SystemId, or something.
        void registerPipeline(
            IRSystemType systemType,
            std::list<SystemId> pipeline
        ) {
            m_systemPipelinesNew[systemType] = pipeline;
        }

        template <
            SystemName SystemName,
            IRSystemType SystemType,
            typename... Args
        >
        void registerSystemClass(Args&&... args)
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
            typename... Components,
            SystemName SystemName,
            IRSystemType SystemType,
            typename... Args
        >
        SystemId registerSystemClassNew(Args&&... args)
        {
            auto systemInstance = std::make_unique<System<SystemName>>(
                std::forward<Args>(args)...
            );
            m_systems.insert(std::make_pair(SystemName, std::move(systemInstance)));
            m_systemPipelines[SystemType].push_back(SystemName);

            SystemVirtual* newSystem = m_systems[SystemName].get();

            IRProfile::engLogInfo(
                "Registered new system {}",
                static_cast<int>(SystemName)
            );

            return createSystem<Components...>(
                "TEMP_NAME",
                newSystem->tick,
                newSystem->beginExecute,
                newSystem->endExecute
            );
        }



        // template <
        //     SystemName SystemName,
        //     IRSystemType SystemType,
        //     typename TickFunction,
        //     typename... Components
        // >
        // SystemId registerEngineSystemNew(
        //     TickFunction tickFunction,
        //     std::unique_ptr<SystemVirtual> system = nullptr
        // )
        // {
        //     SystemId systemId = m_nextUserSystemId++;
        //     m_virtualSystems[systemId] = std::move(system);
        //     m_userSystemFunctions[systemId] =
        //         SystemUser{
        //             .archetype_ = getArchetype<Components...>(),
        //             .function_ = [tickFunction](ArchetypeNode* node) {
        //                 auto componentsTuple = std::make_tuple(
        //                     std::ref(getComponentData<Components>(node))...
        //                 );
        //                 for(int i = 0; i < node->length_; i++) {
        //                     std::apply([i, &tickFunction](auto&&... components) {
        //                         tickFunction(components[i]...);
        //                     }, componentsTuple);
        //                 }
        //             },
        //             .relation_ = Relation::NONE
        //         };
        //     m_systemPipelines[SystemType].push_back(SystemName);
        //     IRProfile::engLogInfo(
        //         "Registered new system {}",
        //         static_cast<int>(SystemName)
        //     );

        // }

        template <
            typename... Components,
            typename Function
        >
        SystemId registerUserSystem(
            std::string name,
            Function function
        )
        {
            // m_userSystemFunctions[m_nextUserSystemId] =
            //     SystemUser{
            //         .archetype_ = getArchetype<Components...>(),
            //         .function_ = [function](ArchetypeNode* node) {
            //             auto componentsTuple = std::make_tuple(
            //                 std::ref(getComponentData<Components>(node))...
            //             );
            //             for(int i = 0; i < node->length_; i++) {
            //                 std::apply([i, &function](auto&&... components) {
            //                     function(components[i]...);
            //                 }, componentsTuple);
            //             }
            //         },
            //         .relation_ = Relation::NONE
            //     };
            // IRProfile::engLogInfo(
            //     "Registered new user system {}",
            //     name,
            //     m_nextUserSystemId
            // );
            // m_nextUserSystemId++;
            // return m_nextUserSystemId - 1;
            return kNullEntity;
        }

        template <IRSystemType systemType>
        void executeGroup() {
            auto& systemOrder = getSystemExecutionOrder<systemType>();
            for(const auto systemName : systemOrder) {
                executeSystem(m_systems[systemName]);
            }
        }

        void executePipeline(IRSystemType systemType) {
            auto& systemOrder = m_systemPipelinesNew[systemType];
            for(SystemId system : systemOrder) {
                executeSystem(system);
            }
        }

        // void executeUserSystemAll() {
        //     IR_PROFILE_FUNCTION();
        //     for(auto& system : m_userSystemFunctions) {
        //         executeUserSystem(system.second);
        //     }
        // }

        void executeUserSystem(
            SystemUser& system
        );
        void executeSystemTick(
            SystemVirtual* system,
            ArchetypeNode* node
        );

        void executeSystem(SystemId system);

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

        template <typename Tag>
        void addSystemTag(SystemId system) {
            m_ticks[system].archetype_.insert(
                IRECS::getComponentType<Tag>()
            );
        }

        // This concept will go away and systems that were needed like
        // This will have their logic moved out to other parts of the
        // Engine.
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
        SystemId m_nextSystemId = 0;
        std::vector<C_Name> m_systemNames;
        std::vector<C_SystemEvent<BEGIN_TICK>> m_beginTicks;
        std::vector<C_SystemEvent<TICK>> m_ticks;
        std::vector<C_SystemEvent<END_TICK>> m_endTicks;
        std::vector<C_SystemRelation> m_relations;
        std::unordered_map<SystemName, SystemId> m_engineSystemIds;
        std::unordered_map<SystemName, std::unique_ptr<SystemVirtual>>
            m_systems;
        std::unordered_map<SystemId, std::unique_ptr<SystemVirtual>> m_virtualSystems;
        std::unordered_map<IRSystemType, std::list<SystemName>>
            m_systemPipelines; // TODO remove
        std::unordered_map<IRSystemType, std::list<SystemId>>
            m_systemPipelinesNew;

        // std::unordered_map<
        //     SystemId,
        //     SystemUser
        // > m_userSystemFunctions;

        void executeSystem(
            std::unique_ptr<SystemVirtual> &system
        );

        template <IRSystemType systemType>
        const std::list<SystemName>& getSystemExecutionOrder() const {
            return m_systemPipelines.at(systemType);
        }
    };

} // namespace IRSystem

#endif /* SYSTEM_MANAGER_H */
