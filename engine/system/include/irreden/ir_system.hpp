#ifndef IR_SYSTEM_H
#define IR_SYSTEM_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/system/ir_assert_main_thread.hpp>
#include <irreden/system/ir_system_types.hpp>
#include <irreden/system/system_access.hpp>
#include <irreden/system/system_manager.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <tuple>
#include <type_traits>

namespace IRSystem {
extern SystemManager *g_systemManager;
SystemManager &getSystemManager();

namespace detail {

// Re-expand a TypeList<Cs...> back into the include-pack of
// SystemManager::createSystem so the matched archetype + dispatch only
// see the real components — the caller passes a `FilterTags_t`-filtered
// list, so neither Exclude<...> placeholders nor tag types
// (ParallelSafe / Spawns / ...) reach the archetype matcher.
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
// rules, ordered most-specific-first so that a variadic catch-all tick
// (which simultaneously satisfies every probe) emits the most useful
// diagnostic. Distilled from the multithreading epic (#226 §"Layer 4"):
//
//   - PARALLEL_FOR + isRelationForm_ → FATAL. The relation branch in
//     `rangedFn` calls `getRelatedEntityFromArchetype` +
//     `getComponentOptional` on `EntityManager` inside the per-row
//     loop; those lookups race on the manager's archetype map from
//     worker threads. Checked first: the most specific form constraint.
//   - PARALLEL_FOR + isBatchForm_ → FATAL. The per-archetype batch
//     form consumes the whole column; row-level chunking would
//     re-enter the body N times with overlapping handles.
//   - PARALLEL_FOR + usesEntityId_ + !parallelSafe_ → FATAL. The
//     per-entity-id tick form passes the iterated EntityId to the
//     body; without an explicit `ParallelSafe` opt-in, the body is
//     assumed to use the id to mutate non-thread-safe singletons
//     (`g_entityManager`, render managers, sol2).
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
        !access.isBatchForm_,
        "System '{}' requested Concurrency::PARALLEL_FOR with the "
        "per-archetype batch tick form. The batch form consumes the "
        "whole entity column; row-level chunking would re-enter the "
        "body with overlapping data.",
        name
    );
    IR_ASSERT(
        !access.usesEntityId_ || access.parallelSafe_,
        "System '{}' requested Concurrency::PARALLEL_FOR with an "
        "EntityId tick parameter but no IRSystem::ParallelSafe tag. The "
        "id-aware tick form is presumed to look up other entities; tag "
        "the component pack with `ParallelSafe` after auditing the body.",
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
//
// #2404: trailing `cadence` / `offset` opt the system into throttled
// dispatch — run 1-in-`cadence` phase ticks (1 = every tick), staggered
// by `offset` (0..cadence-1) against co-registered siblings. Off-cadence
// ticks skip the whole dispatch; the runtime setters live on
// SystemManager / the free functions below.
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
    int grainSize = kDefaultGrainSize,
    std::uint32_t cadence = 1,
    std::uint32_t offset = 0
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

    // Strip tag types (ParallelSafe / Spawns / Destroys / MainThread /
    // AlsoReads / AlsoWrites) AND Exclude<...> out of the included pack
    // before it reaches the archetype matcher + dispatch binder — a tag
    // leaking into getArchetype<...> breaks the match, and a tag in the tick
    // signature static_asserts. `FilterTags_t` is a provable no-op for every
    // tag-free / Exclude-only system: FilterTags_t<Pack...> ==
    // PartitionExcludes<Pack...>::Included whenever the pack carries no tag
    // types (proof: test/system/system_access_test.cpp). The Excluded half
    // (`Partition::Excluded`, above) still drives the exclude archetype.
    return detail::CallCreateSystem<detail::FilterTags_t<TickComponents...>>::run(
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
        cadence,
        offset,
        accessDescriptor
    );
}

// Create a prefab system
//
// #2526: this and `registerSystem<N, ...>` below are the only registration
// entry points that have the `SystemName` statically, so both record the
// resulting id in SystemManager's registry. They nest — `System<type>::create()`
// often calls `registerSystem<type, ...>` — and re-recording the same id is a
// deliberate no-op; see `SystemManager::recordEngineSystemId`.
template <SystemName type, typename... Args> SystemId createSystem(Args &&...args) {
    const SystemId id = System<type>::create(args...);
    getSystemManager().recordEngineSystemId(type, id);
    return id;
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

// #2404: detect `static constexpr std::uint32_t kCadence` / `kCadenceOffset`
// members on a System<N> specialization, mirroring kConcurrency / kGrainSize.
// Absent → cadence 1 (every tick) / offset 0, so every legacy spec is
// unchanged.
template <typename T>
concept HasCadenceMember = requires {
    { T::kCadence } -> std::convertible_to<std::uint32_t>;
};

template <typename T>
concept HasCadenceOffsetMember = requires {
    { T::kCadenceOffset } -> std::convertible_to<std::uint32_t>;
};

template <typename T> constexpr std::uint32_t cadenceOf() {
    if constexpr (HasCadenceMember<T>) {
        return T::kCadence;
    } else {
        return 1u;
    }
}

template <typename T> constexpr std::uint32_t cadenceOffsetOf() {
    if constexpr (HasCadenceOffsetMember<T>) {
        return T::kCadenceOffset;
    } else {
        return 0u;
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
    // Filter tag types out of the member-tick signature too (mirrors
    // createSystem). A `ParallelSafe` in the pack must not surface as a
    // `tick(..., ParallelSafe&)` parameter — `FilterTags_t` drops it while
    // staying byte-identical to `PartitionExcludes::Included` for tag-free
    // packs. The `createSystem<Components...>` call below still sees the full
    // pack, so the tag still flips `parallelSafe_` in the access descriptor.
    using IncludedList = detail::FilterTags_t<Components...>;

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

    // #2404: a System<N> spec opts into throttled dispatch by declaring
    // `static constexpr std::uint32_t kCadence = ...;` (and optionally
    // `kCadenceOffset`). Detectors default to cadence 1 / offset 0, so
    // legacy specs are unchanged.
    constexpr std::uint32_t cadence = detail::cadenceOf<SystemT>();
    constexpr std::uint32_t cadenceOffset = detail::cadenceOffsetOf<SystemT>();

    SystemId id = createSystem<Components...>(
        std::move(name),
        std::move(tickFn),
        std::move(beginFn),
        std::move(endFn),
        std::move(relationParams),
        std::move(relationFn),
        concurrency,
        grainSize,
        cadence,
        cadenceOffset
    );
    setSystemParams(id, std::move(instance));
    getSystemManager().recordEngineSystemId(N, id);
    return id;
}

// #2526: resolve a `SystemName` to the id it was registered under, or
// `IREntity::kNullEntity` if no enum-templated registration recorded it.
// This is the manager-owned replacement for the wire-once
// `setSystem(id)` / `setAllocatorSystem(id)` handles prefab headers used to
// carry: registration self-wires, so a creation no longer has to remember a
// bookkeeping call after `create()`.
//
// Costs one hash lookup. Fine for per-operation callers; a per-entity tick
// should resolve once in `beginTick` and cache the pointer (the ECS footgun
// rule) rather than calling this per row.
inline SystemId findSystem(SystemName name) {
    return getSystemManager().findEngineSystem(name);
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

// One-shot query execution — the run-now counterpart to `createSystemDynamic`
// (#17). Runs `body` once per archetype node matching `(includeArchetype,
// excludeArchetype)`, then returns. Nothing is registered: no `SystemId`, no
// pipeline slot, no timing / concurrency state. The traversal shares
// `IREntity::queryArchetypeNodesSimple` with `executeSystem`, so the node-match
// semantics are identical, but the SystemManager never sees the call — this is
// "run a system tick function one time" without the persistent machinery.
//
// Serial, main-thread-only (same rationale as `createSystemDynamic`'s
// `PARALLEL_FOR` assert — `getComponentData`'s manager hash lookups aren't
// worker-safe). Structural changes inside `body` must use the `IREntity::
// deferred*` API; this primitive does NOT flush (the pipeline's group boundary
// owns that). Zero matches is a silent no-op — correct for a command.
template <typename PerNodeFn>
void executeQueryDynamic(
    const IREntity::Archetype &includeArchetype,
    const IREntity::Archetype &excludeArchetype,
    PerNodeFn &&body
) {
    IR_ASSERT_MAIN_THREAD();
    auto nodes = IREntity::queryArchetypeNodesSimple(includeArchetype, excludeArchetype);
    for (auto *node : nodes) {
        body(node);
    }
}

namespace detail {

// Per-row dispatch for `executeQuery<Cs...>`. Resolves each component column
// ONCE per node (`getComponentData` is a per-call manager lookup — hoisting it
// out of the row loop is the same "no getComponent in a tick" discipline the
// scheduler follows), then invokes the tick per row. Two forms, mirroring
// `createSystem`'s per-component and per-entity-id signatures; whole-node batch
// consumers call `executeQueryDynamic` directly.
template <typename L> struct RunQueryRows;
template <typename... Cs> struct RunQueryRows<TypeList<Cs...>> {
    template <typename FunctionTick>
    static void
    run(const IREntity::Archetype &includeArchetype,
        const IREntity::Archetype &excludeArchetype,
        FunctionTick &&tick) {
        executeQueryDynamic(
            includeArchetype,
            excludeArchetype,
            [&tick](IREntity::ArchetypeNode *node) {
                auto columns = std::tie(IREntity::getComponentData<Cs>(node)...);
                if constexpr (InvocableWithComponents<FunctionTick, Cs...>) {
                    for (int i = 0; i < node->length_; ++i) {
                        std::apply([&](auto &...cols) { tick(cols[i]...); }, columns);
                    }
                } else if constexpr (InvocableWithEntityId<FunctionTick, Cs...>) {
                    auto &entities = node->entities_;
                    for (int i = 0; i < node->length_; ++i) {
                        std::apply([&](auto &...cols) { tick(entities[i], cols[i]...); }, columns);
                    }
                } else {
                    static_assert(
                        false,
                        "executeQuery<Cs...>: the tick must match tick(Cs&...) or "
                        "tick(EntityId, Cs&...). For whole-archetype batch access, "
                        "call executeQueryDynamic directly."
                    );
                }
            }
        );
    }
};

} // namespace detail

// Compile-time-typed one-shot query — the run-now counterpart to
// `createSystem<Cs...>` (#17). `QueryComponents...` may include `Exclude<...>`
// markers, partitioned out at compile time exactly like `createSystem` (so
// tagged entities skip the query with no per-entity branching). Resolves the
// include / exclude archetypes, then dispatches the tick per matched row via
// `executeQueryDynamic`. See `executeQueryDynamic` for the threading +
// deferred-ops contract.
//
//     IRSystem::executeQuery<C_VoxelSetNew, IRSystem::Exclude<C_Locked>>(
//         [](C_VoxelSetNew &set) { set.editVoxels(...); });
template <typename... QueryComponents, typename FunctionTick>
void executeQuery(FunctionTick &&tick) {
    using Partition = detail::PartitionExcludes<QueryComponents...>;
    IREntity::Archetype excludeArchetype =
        detail::ArchetypeFromList<typename Partition::Excluded>::value();
    using IncludedList = detail::FilterTags_t<QueryComponents...>;
    IREntity::Archetype includeArchetype = detail::ArchetypeFromList<IncludedList>::value();
    detail::RunQueryRows<IncludedList>::run(
        includeArchetype,
        excludeArchetype,
        std::forward<FunctionTick>(tick)
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
void registerPipelineGroups(IRTime::Events event, std::vector<std::vector<SystemId>> groups);

/// #1540: append a single system to the end of `event`'s already-
/// registered pipeline as its own serial group, without disturbing the
/// systems already registered for `event`. Unlike `registerPipeline` /
/// `registerPipelineGroups` (which *replace* the event's whole system
/// list), this composes onto a live pipeline — the supported path for a
/// creation whose C++ `initSystems()` ran before a Lua script wants to
/// add an UPDATE / RENDER system. See `engine/system/CLAUDE.md`
/// "Appending to a live pipeline".
void appendToPipeline(IRTime::Events event, SystemId system);

/// #1540: insert a single system as its own serial group immediately
/// before / after the group containing `anchor` in `event`'s pipeline.
/// The position-aware sibling of `appendToPipeline` for when ordering
/// relative to an existing system matters.
void insertIntoPipelineBefore(IRTime::Events event, SystemId system, SystemId anchor);
void insertIntoPipelineAfter(IRTime::Events event, SystemId system, SystemId anchor);

/// T-224: run the cross-system access-conflict validator across every
/// registered pipeline group. FATALs on the first conflict, naming
/// both systems + the offending component. The engine calls this
/// from `World::start()`; tests can call it directly to exercise a
/// hand-built pipeline.
void validateAllPipelineGroups();

/// #1814: clear `event`'s pipeline (no systems run for it). The
/// scene-transition counterpart to `registerPipeline` — a scene machine
/// clears the previous scene's pipeline before registering the next scene's.
/// Lua: `IRSystem.clearPipeline(event)`.
void clearPipeline(IRTime::Events event);

void executePipeline(IRTime::Events event);

// #2404: per-system update cadence. Run a system on 1-in-`cadence` phase
// ticks (1 = every tick, the default); off-cadence ticks skip the entire
// dispatch. `offset` (0..cadence-1) staggers the initial phase so sibling
// systems don't spike on the same tick. Both are settable at runtime with
// no re-registration; a cadence change re-phases from the last run, an
// offset change re-staggers from the current phase tick.
inline void setSystemCadence(SystemId system, std::uint32_t cadence) {
    getSystemManager().setSystemCadence(system, cadence);
}
inline std::uint32_t getSystemCadence(SystemId system) {
    return getSystemManager().getSystemCadence(system);
}
inline void setSystemCadenceOffset(SystemId system, std::uint32_t offset) {
    getSystemManager().setSystemCadenceOffset(system, offset);
}
inline std::uint32_t getSystemCadenceOffset(SystemId system) {
    return getSystemManager().getSystemCadenceOffset(system);
}

// #2404: phase ticks covered by the system's current / most-recent
// execution — the multiplier a throttled integrator reads to stay
// numerically correct at the reduced rate (>= 1 once it has run).
inline std::uint64_t getAccumulatedTicks(SystemId system) {
    return getSystemManager().getAccumulatedTicks(system);
}

// #2404: accumulated fixed-step delta since the system's previous
// execution. UPDATE-phase-only: `IRTime::deltaTime(UPDATE)` is the constant
// fixed step. A RENDER-phase throttled consumer must use raw
// `getAccumulatedTicks` — RENDER dt is wall-clock-variable, so a scaled
// value would be meaningless. Asserts if the TimeManager is not
// initialised (the fixed dt has no meaning without the running loop);
// use `getAccumulatedTicks` in a pure-scheduler unit test.
inline double accumulatedDeltaTime(SystemId system) {
    return static_cast<double>(getSystemManager().getAccumulatedTicks(system)) *
           IRTime::deltaTime(IRTime::UPDATE);
}

// #2404: convert a target sub-rate (Hz) to the nearest integer cadence
// divisor of the engine's fixed UPDATE rate. `targetHz <= 0` clamps to 1
// (every tick). Sugar over the integer-divisor primitive — not a second
// scheduling mode.
inline std::uint32_t cadenceFromRate(double targetHz) {
    if (targetHz <= 0.0) {
        return 1u;
    }
    const double divisor = static_cast<double>(IRConstants::kFPS) / targetHz;
    const long rounded = static_cast<long>(divisor + 0.5); // round-half-up (divisor > 0)
    return rounded < 1 ? 1u : static_cast<std::uint32_t>(rounded);
}

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
