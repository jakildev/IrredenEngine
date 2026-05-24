#ifndef COMPONENT_SYSTEM_EVENT_H
#define COMPONENT_SYSTEM_EVENT_H

#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/system/ir_system_types.hpp>

#include <functional>
#include <vector>

namespace IRComponents {

template <IRSystem::SystemEvent event> struct C_SystemEvent;

template <> struct C_SystemEvent<IRSystem::BEGIN_TICK> {
    std::function<void()> functionBeginTick_;

    // std::vector<std::function<void(IRECS::ArchetypeNode*)>> tickFunctions_ ;

    C_SystemEvent(const std::function<void()> &function)
        : functionBeginTick_(function) {}
    C_SystemEvent()
        : functionBeginTick_() {}
};

template <> struct C_SystemEvent<IRSystem::TICK> {
    /// Whole-node dispatch — body iterates `[0, node->length_)` internally.
    using TickFn = std::function<void(IREntity::ArchetypeNode *)>;
    /// Range-aware dispatch — body covers `[rangeBegin, rangeEnd)` only.
    using RangedTickFn = std::function<void(IREntity::ArchetypeNode *, int, int)>;

    /// Whole-node dispatch — the body iterates `[0, node->length_)`
    /// internally. The legacy form; every system has one. For
    /// `Concurrency::SERIAL` and `Concurrency::MAIN_THREAD` this is the
    /// path SystemManager invokes.
    TickFn functionTick_;

    /// Range-aware dispatch — `[rangeBegin, rangeEnd)` substitutes the
    /// internal loop bounds. Populated by `createSystem<...>` (template
    /// path) for tick signatures the runtime can chunk safely; empty
    /// for `createSystemDynamic` (the Lua-driven dynamic body is opaque
    /// to chunking) and for the per-archetype batch form (the body
    /// consumes the whole column). `Concurrency::PARALLEL_FOR` requires
    /// this slot to be populated; the validator rejects otherwise.
    RangedTickFn rangedFunctionTick_;

    IREntity::Archetype archetype_;
    IREntity::Archetype excludeArchetype_;

    C_SystemEvent(
        TickFn tickFunction,
        RangedTickFn rangedTickFunction,
        IREntity::Archetype archetype,
        IREntity::Archetype excludeArchetype = {}
    )
        : functionTick_(std::move(tickFunction))
        , rangedFunctionTick_(std::move(rangedTickFunction))
        , archetype_(std::move(archetype))
        , excludeArchetype_(std::move(excludeArchetype)) {}

    /// Legacy two-arg overload — used by `createSystemDynamic`. Leaves
    /// `rangedFunctionTick_` empty (validator rejects PARALLEL_FOR).
    C_SystemEvent(
        TickFn tickFunction,
        IREntity::Archetype archetype,
        IREntity::Archetype excludeArchetype = {}
    )
        : functionTick_(std::move(tickFunction))
        , rangedFunctionTick_()
        , archetype_(std::move(archetype))
        , excludeArchetype_(std::move(excludeArchetype)) {}

    C_SystemEvent()
        : functionTick_()
        , rangedFunctionTick_() {}
};

template <> struct C_SystemEvent<IRSystem::END_TICK> {
    std::function<void()> functionEndTick_;

    C_SystemEvent(const std::function<void()> &function)
        : functionEndTick_(function) {}
    C_SystemEvent()
        : functionEndTick_() {}
};

template <> struct C_SystemEvent<IRSystem::RELATION_TICK> {
    std::function<void(IREntity::EntityRecord)> functionRelationTick_;
    IREntity::Archetype archetype_;

    C_SystemEvent(const std::function<void(IREntity::EntityRecord)> &relationTickFunction)
        : functionRelationTick_(relationTickFunction) {}

    C_SystemEvent()
        : functionRelationTick_() {}
};

} // namespace IRComponents

#endif /* COMPONENT_SYSTEM_EVENT_H */
