/*
 * Project: Irreden Engine
 * File: ir_system.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_SYSTEM_H
#define IR_SYSTEM_H

#include <irreden/ir_time.hpp>

#include <irreden/system/ir_system_types.hpp>
#include <irreden/system/system_manager.hpp>

namespace IRECS {
    extern SystemManager* g_systemManager;
    SystemManager& getSystemManager();

    // Create a new system
    template <
        typename... TickComponents,
        typename... TickRelationComponents,
        typename FunctionTick,
        typename FunctionBeginTick = std::nullptr_t,
        typename FunctionEndTick = std::nullptr_t
    >
    constexpr SystemId createSystem(
        std::string name,
        FunctionTick functionTick,
        FunctionBeginTick functionBeginTick = nullptr,
        FunctionEndTick functionEndTick = nullptr,
        RelationParams<TickRelationComponents...> extraParams = {}
    )
    {
        return getSystemManager().createSystem<TickComponents...>(
            name,
            functionTick,
            functionBeginTick,
            functionEndTick,
            extraParams
        );
    }

    // Create a prefab system
    template <
        SystemName type,
        typename... Args
    >
    SystemId createSystem(Args&&... args) {
        return System<type>::create(
            args...
        );
    }

    // TODO: Make extra param as well
    template <
        typename ComponentTag
    >
    void addSystemTag(SystemId system) {
        getSystemManager().addSystemTag<ComponentTag>(system);
    }

    void registerPipeline(
        IRTime::Events systemType,
        std::list<SystemId> pipeline
    );
    void executePipeline(IRTime::Events event);

} // namespace System

#endif /* IR_SYSTEM_H */
