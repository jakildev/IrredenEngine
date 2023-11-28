#ifndef IR_SYSTEM_H
#define IR_SYSTEM_H

#include <irreden/system/ir_system_types.hpp>
#include <irreden/system/system_manager.hpp>

namespace IRECS {
    extern SystemManager* g_systemManager;
    SystemManager& getSystemManager();

    // Used for creating a engine "built-in" system
    template <
        SystemName type,
        typename... Args
    >
    SystemId createSystem(Args&&... args) {
        return System<type>::create(
            args...
        );
    }

    // Used for creating a user-defined system
    template <
        typename... TickComponents,
        typename FunctionTick,
        typename FunctionBeginTick = std::nullptr_t,
        typename FunctionEndTick = std::nullptr_t
    >
    constexpr SystemId createSystem(
        std::string name,
        FunctionTick functionTick,
        FunctionBeginTick functionBeginTick = nullptr,
        FunctionEndTick functionEndTick = nullptr,
        Relation relation = Relation::NONE
    )
    {
        return getSystemManager().createSystem<TickComponents...>(
            name,
            functionTick,
            functionBeginTick,
            functionEndTick,
            relation
        );
    }

    // Currently the alternative to just writing a "tick"
    // lambda. Each call is executed on a single archetype node,
    // and there is more flexability when dealing with the entities'
    // archetypes, heirarchies, etc.
    // TODO: Make something better for heirarchical systems / every other case
    template <
        typename... Components,
        typename FunctionTick,
        typename FunctionBeginTick = std::nullptr_t,
        typename FunctionEndTick = std::nullptr_t
    >
    constexpr SystemId createNodeSystem(
        std::string name,
        FunctionTick functionTick,
        FunctionBeginTick functionBeginTick = nullptr,
        FunctionEndTick functionEndTick = nullptr,
        Relation relation = Relation::NONE
    )
    {
        return getSystemManager().createNodeSystem<Components...>(
            name,
            functionTick,
            functionBeginTick,
            functionEndTick,
            relation
        );
    }

    template <
        typename ComponentTag
    >
    void addSystemTag(SystemId system) {
        getSystemManager().addSystemTag<ComponentTag>(system);
    }

    void executePipeline(SystemTypes systemType);

} // namespace System

#endif /* IR_SYSTEM_H */
