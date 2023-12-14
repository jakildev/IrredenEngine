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

        // TODO: Consolidate createSystem and createNodeSystem
        // TODO: Make parameters a struct and check for specific fields
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
            CreateSystemExtraParams extraParams = {}
        )
        {
            m_systemNames.emplace_back(C_Name{name});

            insertBeginTickFunction(functionBeginTick);
            insertTickFunction<Components...>(
                functionTick,
                extraParams
            );
            insertEndTickFunction(functionEndTick);

            m_relations.emplace_back(
                C_SystemRelation{extraParams.relation_}
            );
            return m_nextSystemId++;
        }


        // TODO: return an id, SystemId, EntityId, or something.
        // Pipelines should be able to take in arbirary data
        // and return arbitrary data.
        // This is currently done by transforming components I suppose
        void registerPipeline(
            SystemTypes systemType,
            std::list<SystemId> pipeline
        ) {
            m_systemPipelinesNew[systemType] = pipeline;
        }


        void executePipeline(SystemTypes systemType) {
            auto& systemOrder = m_systemPipelinesNew[systemType];
            for(SystemId system : systemOrder) {
                executeSystem(system);
            }
        }

        void executeSystem(SystemId system);


        template <typename Tag>
        void addSystemTag(SystemId system) {
            m_ticks[system].archetype_.insert(
                IRECS::getComponentType<Tag>()
            );
        }

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

        std::unordered_map<SystemTypes, std::list<SystemId>>
            m_systemPipelinesNew;

        template <typename FunctionBeginTick>
        void insertBeginTickFunction(
            FunctionBeginTick functionBeginTick
        )
        {
            if constexpr (std::is_invocable_v<FunctionBeginTick>) {
                m_beginTicks.emplace_back(
                    C_SystemEvent<BEGIN_TICK>{
                        [functionBeginTick]() {
                            functionBeginTick();
                        }
                    }
                );
            }
            else {
                m_beginTicks.emplace_back(
                    C_SystemEvent<BEGIN_TICK>{
                        [](){ return; }
                    }
                );
            }
        }

        template <typename... Components, typename FunctionTick>
        void insertTickFunction(
            FunctionTick functionTick,
            CreateSystemExtraParams extraParams
        )
        {
            if constexpr (
                std::is_invocable_v<
                    FunctionTick,
                    EntityId&,
                    Components&...
                >
            )
            {
                m_ticks.emplace_back(
                    C_SystemEvent<TICK>{
                        [functionTick](ArchetypeNode* node) {
                            auto componentsTuple = std::make_tuple(
                                std::ref(node->entities_),
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
                return;
            }

            else if constexpr (
                std::is_invocable_v<
                    FunctionTick,
                    const Archetype&,
                    std::vector<EntityId>&,
                    std::vector<Components>&...
                >
            )
            {
                m_ticks.emplace_back(
                    C_SystemEvent<TICK>{
                        [functionTick](ArchetypeNode* node) {
                            auto paramTuple = std::make_tuple(
                                node->type_,
                                std::ref(node->entities_),
                                std::ref(getComponentData<Components>(node))...
                            );
                            std::apply(
                                [&functionTick](auto&&... args) {
                                    functionTick(
                                        std::forward<decltype(args)>(args)...
                                    );
                                },
                                paramTuple
                            );
                        },
                        getArchetype<Components...>()
                    }
                );
                return;
            }
            else if constexpr (
                std::is_invocable_v<
                    FunctionTick,
                    Components&...
                >
            )
            {
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
            }
            else {
                IR_ASSERT(false, "Invalid tick function signature.");
            }
        }

        template <typename FunctionEndTick>
        void insertEndTickFunction(
            FunctionEndTick functionEndTick
        )
        {
            if constexpr (std::is_invocable_v<FunctionEndTick>) {
                m_endTicks.emplace_back(
                    C_SystemEvent<END_TICK>{
                        [functionEndTick]() {
                            functionEndTick();
                        }
                    }
                );
            }
            else {
                m_endTicks.emplace_back(
                    C_SystemEvent<END_TICK>{
                        [](){ return; }
                    }
                );
            }
        }

    };

} // namespace IRSystem

#endif /* SYSTEM_MANAGER_H */
