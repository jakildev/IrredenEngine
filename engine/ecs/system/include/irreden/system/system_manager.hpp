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

        template <
            typename... Components,
            typename... RelationComponents,
            typename FunctionTick,
            typename FunctionBeginTick = std::nullptr_t,
            typename FunctionEndTick = std::nullptr_t
        >
        SystemId createSystem(
            std::string name,
            FunctionTick functionTick,
            FunctionBeginTick functionBeginTick = nullptr,
            FunctionEndTick functionEndTick = nullptr,
            RelationParams<RelationComponents...> extraParams = {}
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

        template <typename Tag>
        void addSystemTag(SystemId system) {
            m_ticks[system].archetype_.insert(
                IRECS::getComponentType<Tag>()
            );
        }

        void registerPipeline(
            IRTime::Events event,
            std::list<SystemId> pipeline
        );
        void executePipeline(IRTime::Events event);
        void executeSystem(SystemId system);


    private:
        SystemId m_nextSystemId = 0;
        std::vector<C_Name> m_systemNames;
        std::vector<C_SystemEvent<BEGIN_TICK>> m_beginTicks;
        std::vector<C_SystemEvent<TICK>> m_ticks;
        std::vector<C_SystemEvent<END_TICK>> m_endTicks;
        std::vector<C_SystemRelation> m_relations;
        std::unordered_map<SystemName, SystemId> m_engineSystemIds;

        std::unordered_map<IRTime::Events, std::list<SystemId>>
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

        template <
            typename... Components,
            typename... RelationComponents,
            typename FunctionTick
        >
        void insertTickFunction(
            FunctionTick functionTick,
            RelationParams<RelationComponents...> extraParams
        )
        {
            m_ticks.emplace_back(
                C_SystemEvent<TICK>{
                    [functionTick, extraParams](ArchetypeNode* node) {
                        if constexpr (
                            InvocableWithEntityId<
                                FunctionTick,
                                Components...
                            >
                        )
                        {
                            auto componentsTuple = std::make_tuple(
                                std::ref(node->entities_),
                                std::ref(getComponentData<Components>(node))...
                            );
                            for(int i = 0; i < node->length_; i++) {
                                std::apply([i, &functionTick](auto&&... components) {
                                    functionTick(components[i]...);
                                }, componentsTuple);
                            }
                        }
                        else if constexpr (
                            InvocableWithComponents<
                                FunctionTick,
                                Components...
                            >
                        )
                        {
                            auto componentsTuple = std::make_tuple(
                                std::ref(getComponentData<Components>(node))...
                            );
                            for(int i = 0; i < node->length_; i++) {
                                std::apply([i, &functionTick](auto&&... components) {
                                    functionTick(components[i]...);
                                }, componentsTuple);
                            }
                        }
                        else if constexpr (
                            std::is_invocable_v<
                                FunctionTick,
                                Components&...,
                                std::optional<RelationComponents*>...
                            >
                        )
                        {
                            auto componentsTuple = std::make_tuple(
                                std::ref(getComponentData<Components>(node))...
                            );
                            EntityId relatedEntity = getRelatedEntityFromArchetype(
                                node->type_,
                                extraParams.relation_
                            );
                            auto relationComponentTuple = std::make_tuple(
                                getComponentOptional<RelationComponents>(relatedEntity)...
                            );

                            for(int i = 0; i < node->length_; i++) {
                                std::apply([&functionTick](
                                    auto&&... args
                                )
                                {
                                    functionTick(args...);
                                }, std::tuple_cat(
                                        std::make_tuple(
                                            std::ref(std::get<
                                                std::vector<Components>&>(
                                                    componentsTuple
                                                )[i])...
                                        ),
                                        relationComponentTuple
                                    )
                                );
                            }
                        }
                        else if constexpr (
                            InvocableWithNodeVectors<
                                FunctionTick,
                                Components...
                            >
                        )
                        {
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
                        }
                        else {
                            static_assert(false, "Invalid tick function signature.");
                        }
                    },
                    getArchetype<Components...>()
                }
            );
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
