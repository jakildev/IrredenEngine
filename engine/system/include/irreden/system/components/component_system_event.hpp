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

    C_SystemEvent(const std::function<void()> &function)
        : functionBeginTick_(function) {}
    C_SystemEvent()
        : functionBeginTick_() {}
};

template <> struct C_SystemEvent<IRSystem::TICK> {
    /// Whole-node dispatch — body iterates `[0, node->length_)` internally.
    using TickFn = std::function<void(IREntity::ArchetypeNode *)>;
    /// Main-thread binder — invoked once per dispatched archetype node
    /// to resolve component vectors and return a worker closure that
    /// covers `[rangeBegin, rangeEnd)` of the resolved rows. See the
    /// `prepareRangedTick_` field below for the full contract.
    using PrepareRangedTickFn =
        std::function<std::function<void(int rangeBegin, int rangeEnd)>(IREntity::ArchetypeNode *)>;

    /// Whole-node dispatch — used ONLY by the per-archetype batch form
    /// (`InvocableWithNodeVectors`) and dynamic systems created via
    /// `createSystemDynamic` / Lua hot-reload. Row-iterating forms
    /// (per-component, per-entity-id) leave this empty and dispatch
    /// entirely through `prepareRangedTick_` (T-348).
    TickFn functionTick_;

    /// Main-thread binder for row-iterating tick forms; used by
    /// SERIAL/MAIN_THREAD and PARALLEL_FOR alike (T-348).
    /// SystemManager calls this **once on the main thread** per matched
    /// archetype node. The binder resolves the per-component vector
    /// references from the node (going through
    /// `EntityManager::getComponentType<>` → `m_pureComponentTypes`
    /// hash lookup, which is NOT safe under concurrent reads from worker
    /// threads), captures them in a `void(rangeBegin, rangeEnd)` closure,
    /// and returns it. For SERIAL/MAIN_THREAD, `executeSystem` invokes
    /// the closure immediately with `[0, length)`. For PARALLEL_FOR,
    /// it fans the closure out to `IRJob::parallelFor`. Populated by the
    /// template `createSystem<...>` path for tick signatures the runtime
    /// can chunk safely; empty for `createSystemDynamic` (opaque body)
    /// and the per-archetype batch form (consumes the whole column) —
    /// the registration validator rejects `PARALLEL_FOR` in both cases.
    PrepareRangedTickFn prepareRangedTick_;

    IREntity::Archetype archetype_;
    IREntity::Archetype excludeArchetype_;

    C_SystemEvent(
        TickFn tickFunction,
        PrepareRangedTickFn prepareRangedTick,
        IREntity::Archetype archetype,
        IREntity::Archetype excludeArchetype = {}
    )
        : functionTick_(std::move(tickFunction))
        , prepareRangedTick_(std::move(prepareRangedTick))
        , archetype_(std::move(archetype))
        , excludeArchetype_(std::move(excludeArchetype)) {}

    /// Legacy two-arg overload — used by `createSystemDynamic`. Leaves
    /// `prepareRangedTick_` empty (validator rejects PARALLEL_FOR).
    C_SystemEvent(
        TickFn tickFunction,
        IREntity::Archetype archetype,
        IREntity::Archetype excludeArchetype = {}
    )
        : functionTick_(std::move(tickFunction))
        , prepareRangedTick_()
        , archetype_(std::move(archetype))
        , excludeArchetype_(std::move(excludeArchetype)) {}

    C_SystemEvent()
        : functionTick_()
        , prepareRangedTick_() {}
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
