#ifndef IR_SYSTEM_H
#define IR_SYSTEM_H

#include <irreden/ir_time.hpp>

#include <irreden/system/ir_system_types.hpp>
#include <irreden/system/system_manager.hpp>

namespace IRSystem {
extern SystemManager *g_systemManager;
SystemManager &getSystemManager();

namespace detail {

// Re-expand a TypeList<Cs...> back into the include-pack of
// SystemManager::createSystem so the matched archetype + dispatch only
// see the real components (not Exclude<...> placeholders).
template <typename L> struct CallCreateSystem;
template <typename... Cs>
struct CallCreateSystem<TypeList<Cs...>> {
    template <typename... Args>
    static SystemId run(SystemManager &mgr, Args &&...args) {
        return mgr.template createSystem<Cs...>(std::forward<Args>(args)...);
    }
};

template <typename L> struct ArchetypeFromList;
template <typename... Cs>
struct ArchetypeFromList<TypeList<Cs...>> {
    static IREntity::Archetype value() { return IREntity::getArchetype<Cs...>(); }
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
    auto excludeArchetype =
        detail::ArchetypeFromList<typename Partition::Excluded>::value();
    return detail::CallCreateSystem<typename Partition::Included>::run(
        getSystemManager(),
        std::move(name),
        std::move(functionTick),
        std::move(functionBeginTick),
        std::move(functionEndTick),
        extraParams,
        std::move(functionRelationTick),
        std::move(excludeArchetype)
    );
}

// Create a prefab system
template <SystemName type, typename... Args> SystemId createSystem(Args &&...args) {
    return System<type>::create(args...);
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

inline void setTimingEnabled(bool enabled) { getSystemManager().setTimingEnabled(enabled); }
inline bool isTimingEnabled() { return getSystemManager().isTimingEnabled(); }
inline void resetTimingStats() { getSystemManager().resetTimingStats(); }

} // namespace IRSystem

#endif /* IR_SYSTEM_H */
