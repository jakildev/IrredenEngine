#ifndef IR_SYSTEM_H
#define IR_SYSTEM_H

#include <irreden/ir_time.hpp>

#include <irreden/system/ir_system_types.hpp>
#include <irreden/system/system_access.hpp>
#include <irreden/system/system_manager.hpp>

#include <functional>
#include <optional>
#include <type_traits>

namespace IRSystem {
extern SystemManager *g_systemManager;
SystemManager &getSystemManager();

namespace detail {

// Re-expand a TypeList<Cs...> back into the include-pack of
// SystemManager::createSystem so the matched archetype + dispatch only
// see the real components (not Exclude<...> placeholders).
template <typename L> struct CallCreateSystem;
template <typename... Cs> struct CallCreateSystem<TypeList<Cs...>> {
    template <typename... Args> static SystemId run(SystemManager &mgr, Args &&...args) {
        return mgr.template createSystem<Cs...>(std::forward<Args>(args)...);
    }
};

template <typename L> struct ArchetypeFromList;
template <typename... Cs> struct ArchetypeFromList<TypeList<Cs...>> {
    static IREntity::Archetype value() {
        return IREntity::getArchetype<Cs...>();
    }
};

// T-222 / T-334: validate that a system's compile-time access
// descriptor is compatible with its requested Concurrency policy. Four
// rules, distilled from the multithreading epic (#226 §"Layer 4"):
//
//   - PARALLEL_FOR + usesEntityId_ + !parallelSafe_ → FATAL. The
//     per-entity-id tick form passes the iterated EntityId to the
//     body; without an explicit `ParallelSafe` opt-in, the body is
//     assumed to use the id to mutate non-thread-safe singletons
//     (`g_entityManager`, render managers, sol2).
//   - PARALLEL_FOR + isBatchForm_ → FATAL. The per-archetype batch
//     form consumes the whole column; row-level chunking would
//     re-enter the body N times with overlapping handles.
//   - PARALLEL_FOR + isRelationForm_ → FATAL. The relation branch in
//     `rangedFn` calls `getRelatedEntityFromArchetype` +
//     `getComponentOptional` on `EntityManager` from inside the per-
//     row loop, which races on the manager's archetype map from
//     worker threads.
//   - PARALLEL_FOR + mainThreadOnly_ → FATAL. The `MainThread` tag is
//     explicit "do not parallelize", and silently downgrading would
//     hide the conflict.
//
// The static_assert flavor would be ideal but the Concurrency value
// is a runtime parameter on the entry-point wrapper, so we IR_ASSERT
// instead. Debug-only — release strips the check, but a PARALLEL_FOR
// system that survives debug-mode CI is also safe in release.
inline void
validateConcurrencyForAccess(const std::string &name, Concurrency c, SystemAccess access) {
    if (c != Concurrency::PARALLEL_FOR) {
        return;
    }
    IR_ASSERT(
        !access.usesEntityId_ || access.parallelSafe_,
        "System '{}' requested Concurrency::PARALLEL_FOR with an "
        "EntityId tick parameter but no IRSystem::ParallelSafe tag. The "
        "id-aware tick form is presumed to look up other entities; tag "
        "the component pack with `ParallelSafe` after auditing the body.",
        name
    );
    IR_ASSERT(
        !access.isBatchForm_,
        "System '{}' requested Concurrency::PARALLEL_FOR with the "
        "per-archetype batch tick form. The batch form consumes the "
        "whole entity column; row-level chunking would re-enter the "
        "body with overlapping data.",
        name
    );
    IR_ASSERT(
        !access.isRelationForm_,
        "System '{}' requested Concurrency::PARALLEL_FOR with the "
        "relation tick form (RelationParams<...> + std::optional<...*> "
        "in the tick signature). The relation branch resolves the "
        "related entity and its components via EntityManager lookups "
        "inside the per-row loop; those manager accesses are not "
        "thread-safe.",
        name
    );
    IR_ASSERT(
        !access.mainThreadOnly_,
        "System '{}' requested Concurrency::PARALLEL_FOR while also "
        "carrying the IRSystem::MainThread tag. Pick one — the tag is "
        "explicit 'do not parallelize'.",
        name
    );
}

} // namespace detail

// Create a new system. `TickComponents...` may include zero or more
// `Exclude<Tags...>` markers; these are partitioned out at compile time
// and used to build an exclude archetype that the matcher rejects nodes
// against (so tagged entities skip this system without per-entity
// branching). See ir_system_types.hpp for the Exclude<> declaration.
//
// T-222: trailing `concurrency` and `grainSize` opt the system into
// the worker-pool dispatch path. `Concurrency::SERIAL` (default)
// matches the legacy behavior; `PARALLEL_FOR` requires the tick body
// to satisfy the validator (`detail::validateConcurrencyForAccess`).
template <
    typename... TickComponents,
    typename... TickRelationComponents,
    typename FunctionTick,
    typename FunctionBeginTick = std::nullptr_t,
    typename FunctionEndTick = std::nullptr_t,
    typename FunctionRelationTick = std::nullptr_t>
constexpr SystemId createSystem(
    std::string name,
    FunctionTick functionTick,
    FunctionBeginTick functionBeginTick = nullptr,
    FunctionEndTick functionEndTick = nullptr,
    RelationParams<TickRelationComponents...> extraParams = {},
    FunctionRelationTick functionRelationTick = nullptr,
    Concurrency concurrency = Concurrency::SERIAL,
    int grainSize = kDefaultGrainSize
) {
    using Partition = detail::PartitionExcludes<TickComponents...>;
    auto excludeArchetype = detail::ArchetypeFromList<typename Partition::Excluded>::value();

    // Derive access descriptor from the tick signature + component
    // pack. The wrapper passes it through so SystemManager records it
    // alongside the Concurrency for the validator + future cross-system
    // validation (T-224). `deriveAccessFromSignature` can't see the
    // relation pack — two ambiguous packs in a free-function template,
    // see the TODO at `InvocableWithOptionalRelations` in
    // ir_system_types.hpp — so we fold `isRelationForm_` in here where
    // both packs are in scope.
    //
    // TODO: const-qualified relation components. If a caller declares
    // `RelationParams<const RelComp>`, the probe below instantiates as
    // `std::optional<const RelComp*>`, which does NOT match the
    // `std::optional<RelComp*>` parameter passed by `rangedFn`'s
    // relation branch (`system_manager.hpp` — search for the relation
    // dispatch site). The probe returns false, `isRelationForm_` stays
    // unset, and the PARALLEL_FOR guard silently doesn't fire. No
    // current system uses `const T` in `RelationParams`, so this is a
    // latent edge case rather than a live bug — strip cv via
    // `std::remove_cvref_t<TickRelationComponents>` here (and at the
    // dispatch site if it ever takes a const pointer) before flipping
    // the bit.
    constexpr SystemAccess accessDescriptor = []() {
        SystemAccess a = deriveAccessFromSignature<FunctionTick, TickComponents...>();
        if constexpr (sizeof...(TickRelationComponents) > 0) {
            if constexpr (
                std::is_invocable_v<
                    FunctionTick,
                    std::remove_cvref_t<TickComponents> &...,
                    std::optional<TickRelationComponents *>...>
            ) {
                a.isRelationForm_ = true;
            }
        }
        return a;
    }();
    detail::validateConcurrencyForAccess(name, concurrency, accessDescriptor);

    return detail::CallCreateSystem<typename Partition::Included>::run(
        getSystemManager(),
        std::move(name),
        std::move(functionTick),
        std::move(functionBeginTick),
        std::move(functionEndTick),
        std::move(extraParams),
        std::move(functionRelationTick),
        std::move(excludeArchetype),
        concurrency,
        grainSize,
        accessDescriptor
    );
}

// Create a prefab system
template <SystemName type, typename... Args> SystemId createSystem(Args &&...args) {
    return System<type>::create(args...);
}

namespace detail {

// Member-on-System<N> hook detection. Each concept is satisfied when
// the System<N> specialization declares the named member function with
// a matching signature. Concepts are defined here so registerSystem<>
// can dispatch to no-op fallbacks when an optional hook is absent.
template <typename T>
concept HasBeginTickMember = requires(T &t) { t.beginTick(); };

template <typename T>
concept HasEndTickMember = requires(T &t) { t.endTick(); };

template <typename T, typename... RelComps>
concept HasRelationTickMember = requires(T &t, RelComps &...rs) { t.relationTick(rs...); };

template <typename T, typename L> struct MakeMemberTickFn;

// Build a tick lambda that forwards to the matching `tick(...)` member
// on the System<N> instance. Three signatures are supported, mirroring
// `createSystem`'s three TICK forms:
//   1. tick(Cs&...)                                  — per-component
//   2. tick(EntityId, Cs&...)                        — per-entity-id
//   3. tick(const Archetype&, std::vector<EntityId>&,
//          std::vector<Cs>&...)                      — per-archetype batch
template <typename T, typename... Cs> struct MakeMemberTickFn<T, TypeList<Cs...>> {
    static auto build(T *p) {
        if constexpr (requires(T &s, Cs &...cs) { s.tick(cs...); }) {
            return [p](Cs &...cs) { p->tick(cs...); };
        } else if constexpr (requires(T &s, EntityId id, Cs &...cs) { s.tick(id, cs...); }) {
            return [p](EntityId id, Cs &...cs) { p->tick(id, cs...); };
        } else if constexpr (
            requires(
                T &s,
                const Archetype &a,
                std::vector<EntityId> &ids,
                std::vector<Cs> &...cols
            ) { s.tick(a, ids, cols...); }
        ) {
            return [p](const Archetype &a, std::vector<EntityId> &ids, std::vector<Cs> &...cols) {
                p->tick(a, ids, cols...);
            };
        } else {
            static_assert(
                false,
                "registerSystem<N, Cs...>: System<N> must declare a tick(...) "
                "member matching one of the three valid signatures: "
                "tick(Cs&...), tick(EntityId, Cs&...), or "
                "tick(const Archetype&, std::vector<EntityId>&, std::vector<Cs>&...)."
            );
        }
    }
};

template <typename T> auto makeMemberBeginTickFn(T *p) {
    if constexpr (HasBeginTickMember<T>) {
        return [p]() { p->beginTick(); };
    } else {
        return nullptr;
    }
}

template <typename T> auto makeMemberEndTickFn(T *p) {
    if constexpr (HasEndTickMember<T>) {
        return [p]() { p->endTick(); };
    } else {
        return nullptr;
    }
}

template <typename T, typename... RelComps> auto makeMemberRelationTickFn(T *p) {
    if constexpr (HasRelationTickMember<T, RelComps...>) {
        return [p](RelComps &...rs) { p->relationTick(rs...); };
    } else {
        return nullptr;
    }
}

// T-222: detect `static constexpr Concurrency kConcurrency` /
// `static constexpr int kGrainSize` members on a System<N>
// specialization. Used by `registerSystem` to opt a system into
// PARALLEL_FOR without forcing every legacy spec to grow boilerplate.
template <typename T>
concept HasConcurrencyMember = requires {
    { T::kConcurrency } -> std::convertible_to<Concurrency>;
};

template <typename T>
concept HasGrainSizeMember = requires {
    { T::kGrainSize } -> std::convertible_to<int>;
};

template <typename T> constexpr Concurrency concurrencyOf() {
    if constexpr (HasConcurrencyMember<T>) {
        return T::kConcurrency;
    } else {
        return Concurrency::SERIAL;
    }
}

template <typename T> constexpr int grainSizeOf() {
    if constexpr (HasGrainSizeMember<T>) {
        return T::kGrainSize;
    } else {
        return kDefaultGrainSize;
    }
}

} // namespace detail

// Register a system whose state lives as **member fields on the
// `System<N>` specialization itself**, with `tick` / `beginTick` /
// `endTick` / `relationTick` as named member functions instead of the
// explicit `Params` + lambda-capture + `setSystemParams` boilerplate.
//
// The lifetime contract is identical to `setSystemParams`: a single
// `std::make_unique<System<N>>()` instance is allocated here, owned by
// the system entity's parameters slot, and freed when the system is
// destroyed. The instance pointer is captured by value into the
// per-tick lambdas — same one-pointer-lookup-per-frame cost as the
// canonical Params pattern.
//
// Usage:
//
//     template <> struct System<MY_NAME> {
//         vec2 cachedMouse_ = vec2(0.0f);   // params live as members
//
//         void beginTick() { cachedMouse_ = IRRender::getMouse(); }
//         void tick(C_HitBox &h, const C_Pos &p) {
//             h.hovered_ = aabbContains(p.pos_, cachedMouse_);
//         }
//
//         static SystemId create() {
//             return registerSystem<MY_NAME, C_HitBox, C_Pos>("MyName");
//         }
//     };
//
// The `Components...` pack supports the same `Exclude<...>` markers as
// `createSystem` (partitioned at compile time). `RelationParams<...>`
// is forwarded the same way; the helper detects a `relationTick`
// member with a signature matching the relation components.
template <SystemName N, typename... Components, typename... RelationComponents>
SystemId
registerSystem(std::string name, RelationParams<RelationComponents...> relationParams = {}) {
    using SystemT = System<N>;
    using IncludedList = typename detail::PartitionExcludes<Components...>::Included;

    auto instance = std::make_unique<SystemT>();
    SystemT *p = instance.get();

    auto tickFn = detail::MakeMemberTickFn<SystemT, IncludedList>::build(p);
    auto beginFn = detail::makeMemberBeginTickFn<SystemT>(p);
    auto endFn = detail::makeMemberEndTickFn<SystemT>(p);
    auto relationFn = detail::makeMemberRelationTickFn<SystemT, RelationComponents...>(p);

    // T-222: a System<N> spec can opt the system into PARALLEL_FOR by
    // declaring `static constexpr Concurrency kConcurrency = ...;`
    // (and optionally `static constexpr int kGrainSize = ...;`). The
    // detectors fall back to SERIAL / kDefaultGrainSize when the spec
    // doesn't declare them — every legacy register-spec stays
    // unchanged.
    constexpr Concurrency concurrency = detail::concurrencyOf<SystemT>();
    constexpr int grainSize = detail::grainSizeOf<SystemT>();

    SystemId id = createSystem<Components...>(
        std::move(name),
        std::move(tickFn),
        std::move(beginFn),
        std::move(endFn),
        std::move(relationParams),
        std::move(relationFn),
        concurrency,
        grainSize
    );
    setSystemParams(id, std::move(instance));
    return id;
}

// Free-function wrapper around `SystemManager::createSystemDynamic`. Used by
// the Lua-driven path where component types are known only at runtime; the
// caller passes resolved archetype sets (lists of `ComponentId`) and a body
// taking the matched `ArchetypeNode*`. Templated `createSystem<...>` remains
// the canonical path for C++ systems; this exists only for runtime-typed
// dispatch (Lua, dynamic systems registered from a creation script).
inline SystemId createSystemDynamic(
    std::string name,
    IREntity::Archetype includeArchetype,
    IREntity::Archetype excludeArchetype,
    std::function<void(IREntity::ArchetypeNode *)> body,
    Concurrency concurrency = Concurrency::SERIAL
) {
    return getSystemManager().createSystemDynamic(
        std::move(name),
        std::move(includeArchetype),
        std::move(excludeArchetype),
        std::move(body),
        concurrency
    );
}

// Free-function wrapper around `SystemManager::replaceSystemBody`. Swap a
// system's per-archetype tick body in place; archetype filter, exclude
// archetype, `SystemParams`, and pipeline registrations are unchanged.
// Used by the Lua hot-reload path (`IRSystem.replaceSystemBody`) and by
// any C++ caller that wants to rebind a dynamic system's body without
// recreating the system entity.
//
// Note: calling this with a plain C++ body on a Lua-registered system's
// id silently disconnects the Lua `IRSystem.replaceSystemBody`
// hot-reload path — the new lambda no longer consults the
// `shared_ptr` tickRef, so subsequent Lua reseats won't take effect.
// Use the Lua surface to hot-reload a Lua-defined system.
inline void
replaceSystemBody(SystemId system, std::function<void(IREntity::ArchetypeNode *)> body) {
    getSystemManager().replaceSystemBody(system, std::move(body));
}

// TODO: Make extra param as well
template <typename ComponentTag> void addSystemTag(SystemId system) {
    getSystemManager().addSystemTag<ComponentTag>(system);
}

// Mirror of addSystemTag<> for the exclude archetype. After this call,
// the matcher rejects any node whose type contains the tag.
template <typename ComponentTag> void addSystemExcludeTag(SystemId system) {
    getSystemManager().addSystemExcludeTag<ComponentTag>(system);
}

template <typename Params> void setSystemParams(SystemId system, std::unique_ptr<Params> params) {
    getSystemManager().setSystemParams(system, std::move(params));
}

template <typename Params> Params *getSystemParams(SystemId system) {
    return getSystemManager().getSystemParams<Params>(system);
}

void registerPipeline(IRTime::Events systemType, std::list<SystemId> pipeline);

/// T-224: register a pipeline as a sequence of parallel groups. Each
/// inner vector is one parallel group — its members run concurrently
/// on the IRJobs worker pool. Groups themselves run in declaration
/// order; `flushStructuralChanges` fires between groups. Call
/// `IRSystem::validateAllPipelineGroups()` after every system + every
/// pipeline is registered (engine does this automatically at
/// `World::start()`). Replaces any prior pipeline registration for
/// `event`.
///
///     IRSystem::registerPipelineGroups(IRTime::Events::UPDATE, {
///         { velocity, drag, gravity },   // group 0: parallel
///         { globalPosition },            // group 1: serial
///         { lifetime },                  // group 2: serial
///     });
void registerPipelineGroups(
    IRTime::Events event, std::vector<std::vector<SystemId>> groups
);

/// T-224: run the cross-system access-conflict validator across every
/// registered pipeline group. FATALs on the first conflict, naming
/// both systems + the offending component. The engine calls this
/// from `World::start()`; tests can call it directly to exercise a
/// hand-built pipeline.
void validateAllPipelineGroups();

void executePipeline(IRTime::Events event);

inline void setTimingEnabled(bool enabled) {
    getSystemManager().setTimingEnabled(enabled);
}
inline bool isTimingEnabled() {
    return getSystemManager().isTimingEnabled();
}
inline void resetTimingStats() {
    getSystemManager().resetTimingStats();
}

inline TickObserverId registerTickObserver(std::unique_ptr<TickObserver> observer) {
    return getSystemManager().registerTickObserver(std::move(observer));
}
inline void unregisterTickObserver(TickObserverId id) {
    getSystemManager().unregisterTickObserver(id);
}

} // namespace IRSystem

#endif /* IR_SYSTEM_H */
