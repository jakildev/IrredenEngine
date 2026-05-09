#ifndef IR_SYSTEM_H
#define IR_SYSTEM_H

#include <irreden/ir_time.hpp>

#include <irreden/system/ir_system_types.hpp>
#include <irreden/system/system_manager.hpp>

#include <functional>

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

} // namespace detail

// Create a new system. `TickComponents...` may include zero or more
// `Exclude<Tags...>` markers; these are partitioned out at compile time
// and used to build an exclude archetype that the matcher rejects nodes
// against (so tagged entities skip this system without per-entity
// branching). See ir_system_types.hpp for the Exclude<> declaration.
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
    FunctionRelationTick functionRelationTick = nullptr
) {
    using Partition = detail::PartitionExcludes<TickComponents...>;
    auto excludeArchetype = detail::ArchetypeFromList<typename Partition::Excluded>::value();
    return detail::CallCreateSystem<typename Partition::Included>::run(
        getSystemManager(),
        std::move(name),
        std::move(functionTick),
        std::move(functionBeginTick),
        std::move(functionEndTick),
        std::move(extraParams),
        std::move(functionRelationTick),
        std::move(excludeArchetype)
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

    SystemId id = createSystem<Components...>(
        std::move(name),
        std::move(tickFn),
        std::move(beginFn),
        std::move(endFn),
        std::move(relationParams),
        std::move(relationFn)
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
    std::function<void(IREntity::ArchetypeNode *)> body
) {
    return getSystemManager().createSystemDynamic(
        std::move(name),
        std::move(includeArchetype),
        std::move(excludeArchetype),
        std::move(body)
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
