#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include <irreden/ir_profile.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/system/ir_system_types.hpp>
#include <irreden/system/system_access.hpp>

#include <irreden/common/components/component_name.hpp>
#include <irreden/system/components/component_system_event.hpp>
#include <irreden/system/components/component_system_relation.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <list>

using namespace IRComponents;

namespace IRSystem {

class ISystemParams {
  public:
    virtual ~ISystemParams() = default;
};

template <typename Params> class ISystemParamsImpl : public ISystemParams {
  public:
    explicit ISystemParamsImpl(std::unique_ptr<Params> params)
        : params_(std::move(params)) {}

    std::unique_ptr<Params> params_;
};

/// Observer hook fired before and after each system tick from the main
/// thread. Used by the render layer to bracket GPU-stage timing samples
/// around per-system work without inlining `device()->finish()` blocks
/// into every tick lambda. Generic on purpose: trace capture or
/// per-system telemetry can plug in using the same hook without
/// touching SystemManager again.
///
/// **Singleton-group only (T-224).** Observer fires bracket each
/// `executeSystem` call ONLY for single-system pipeline groups. Systems
/// scheduled inside a multi-system parallel group dispatch from worker
/// threads (`IRJob::parallelFor`), where the observer surface is
/// undefined: GPU APIs like `device()->finish()` and `writeTimestamp`
/// require main-thread context, and per-system CPU-time samples are
/// meaningless when sibling systems run concurrently. Any system that
/// needs observer brackets must live in a singleton group.
class TickObserver {
  public:
    virtual ~TickObserver() = default;
    virtual void onBeforeTick(SystemId system) = 0;
    virtual void onAfterTick(SystemId system) = 0;
};

struct TickObserverId {
    std::uint32_t value_;
};

class SystemManager {
  public:
    using ErasedParamsPtr = std::unique_ptr<ISystemParams>;

    /// Per-system timing accumulator, populated when timing is enabled.
    struct TimingAccum {
        uint64_t totalNs_ = 0;
        uint64_t minNs_ = UINT64_MAX;
        uint64_t maxNs_ = 0;
        uint32_t callCount_ = 0;
        uint64_t totalEntityCount_ = 0;
    };

    SystemManager();
    ~SystemManager();

    template <
        typename... Components,
        typename... RelationComponents,
        typename FunctionTick,
        typename FunctionBeginTick = std::nullptr_t,
        typename FunctionEndTick = std::nullptr_t,
        typename FunctionRelationTick = std::nullptr_t>
    SystemId createSystem(
        std::string name,
        FunctionTick functionTick,
        FunctionBeginTick functionBeginTick = nullptr,
        FunctionEndTick functionEndTick = nullptr,
        RelationParams<RelationComponents...> extraParams = {},
        FunctionRelationTick functionRelationTick = nullptr,
        Archetype excludeArchetype = {},
        Concurrency concurrency = Concurrency::SERIAL,
        int grainSize = kDefaultGrainSize,
        std::uint32_t cadence = 1,
        std::uint32_t offset = 0,
        SystemAccess accessDescriptor = {}
    ) {
        m_systemNames.emplace_back(C_Name{name});
        SystemId newSystemId = m_nextSystemId++;

        insertBeginTickFunction(functionBeginTick);
        insertTickFunction<Components...>(functionTick, extraParams, std::move(excludeArchetype));
        insertEndTickFunction(functionEndTick);
        insertRelationTickFunction<RelationComponents...>(functionRelationTick);

        m_relations.emplace_back(C_SystemRelation{extraParams.relation_});
        m_systemParams.emplace_back(nullptr);
        m_timingAccum.emplace_back();
        m_concurrency.emplace_back(concurrency);
        m_grainSize.emplace_back(grainSize > 0 ? grainSize : 1);
        m_systemAccess.emplace_back(accessDescriptor);
        emplaceCadenceState(cadence, offset);
        return newSystemId;
    }

    /// Per-system concurrency policy, parallel to `m_ticks`. Read by
    /// `executeSystem` to branch between serial dispatch and the
    /// `IRJob::parallelFor` worker fan-out.
    Concurrency getConcurrency(SystemId system) const {
        return system < m_concurrency.size() ? m_concurrency[system] : Concurrency::SERIAL;
    }

    /// Per-system chunk size for `Concurrency::PARALLEL_FOR`. Tunable
    /// per-system via the trailing `grainSize` parameter to
    /// `createSystem`; defaults to `kDefaultGrainSize` (512).
    int getGrainSize(SystemId system) const {
        return system < m_grainSize.size() ? m_grainSize[system] : kDefaultGrainSize;
    }

    /// Constexpr access descriptor recorded at registration time.
    /// `Concurrency::PARALLEL_FOR` only validates well if this is
    /// populated (the entry-point wrapper in `ir_system.hpp` derives it
    /// from the tick function's signature). Empty for dynamic systems.
    const SystemAccess &getSystemAccess(SystemId system) const {
        static const SystemAccess kEmpty{};
        return system < m_systemAccess.size() ? m_systemAccess[system] : kEmpty;
    }

    /// #2404: per-system update cadence — run the system on 1-in-N phase
    /// ticks of the pipeline it belongs to. `cadence == 1` (default) is
    /// every tick (0 is normalized to 1). Takes effect on the next phase
    /// tick with no re-registration; the change re-phases from the
    /// system's last run (a shorter cadence fires sooner, a longer one
    /// later). Off-cadence ticks skip the ENTIRE dispatch — no
    /// `beginTick`, no archetype query, no per-entity iteration — so the
    /// saving is real, not an in-body early return.
    void setSystemCadence(SystemId system, std::uint32_t cadence);
    std::uint32_t getSystemCadence(SystemId system) const {
        return system < m_cadence.size() ? m_cadence[system] : 1u;
    }

    /// #2404 (amendment 1): initial phase stagger, `0..cadence-1`. Sibling
    /// systems registered together at cadence N with distinct offsets fire
    /// on distinct ticks (smoothed load) instead of spiking on the same
    /// tick. Setting it at runtime re-phases the system so the offset takes
    /// effect as a fresh stagger from the current phase tick.
    void setSystemCadenceOffset(SystemId system, std::uint32_t offset);
    std::uint32_t getSystemCadenceOffset(SystemId system) const {
        return system < m_cadenceOffset.size() ? m_cadenceOffset[system] : 0u;
    }

    /// #2404: phase ticks covered by the system's current / most-recent
    /// execution — the multiplier a throttled integrator reads so its
    /// per-tick-rate math stays correct at the reduced rate. `>= 1` once
    /// the system has run; `0` before its first execution. For UPDATE-
    /// phase systems the accumulated fixed-step delta is this value times
    /// `IRTime::deltaTime(UPDATE)` (see `IRSystem::accumulatedDeltaTime`).
    std::uint64_t getAccumulatedTicks(SystemId system) const {
        return system < m_accumulatedTicks.size() ? m_accumulatedTicks[system] : 0u;
    }

    template <typename Tag> void addSystemTag(SystemId system) {
        m_ticks[system].archetype_.insert(IREntity::getComponentType<Tag>());
    }

    template <typename Tag> void addSystemExcludeTag(SystemId system) {
        m_ticks[system].excludeArchetype_.insert(IREntity::getComponentType<Tag>());
    }

    template <typename Params>
    void setSystemParams(SystemId system, std::unique_ptr<Params> params) {
        m_systemParams[system] = std::make_unique<ISystemParamsImpl<Params>>(std::move(params));
    }

    template <typename Params> Params *getSystemParams(SystemId system) {
        auto *paramsImpl = static_cast<ISystemParamsImpl<Params> *>(m_systemParams[system].get());
        return paramsImpl == nullptr ? nullptr : paramsImpl->params_.get();
    }

    /// Runtime-archetype-typed parallel of `createSystem<...>`. The
    /// include / exclude archetypes are passed as resolved `Archetype`
    /// values (sets of `ComponentId`) rather than C++ template
    /// parameters, and the body is a `std::function` that receives the
    /// matched `ArchetypeNode*` directly. Used by the Lua-driven path
    /// (`LuaScript::registerSystem`) where the component types are
    /// known only at runtime; the body fires once per matched
    /// archetype per tick (no per-entity dispatch by SystemManager —
    /// caller's body decides whether to iterate the columns row-wise
    /// or batch-process them).
    SystemId createSystemDynamic(
        std::string name,
        Archetype includeArchetype,
        Archetype excludeArchetype,
        std::function<void(ArchetypeNode *)> body,
        Concurrency concurrency = Concurrency::SERIAL,
        std::uint32_t cadence = 1,
        std::uint32_t offset = 0
    );

    /// Replace the per-archetype tick body of an existing system in place.
    /// The system's `SystemId`, archetype filter, exclude archetype,
    /// `SystemParams`, and any pipeline registrations are unchanged —
    /// only the function invoked per matched `ArchetypeNode` is swapped.
    /// In-flight entities continue using the new body on the next
    /// pipeline tick with no special handling. Asserts on out-of-range
    /// SystemId; the body must be non-empty (an empty `std::function`
    /// would crash on the next dispatch).
    void replaceSystemBody(SystemId system, std::function<void(ArchetypeNode *)> body);

    void registerPipeline(IRTime::Events event, std::list<SystemId> pipeline);

    /// #1814: empty `event`'s pipeline (no systems run for it). The
    /// scene-transition counterpart to `registerPipeline` — a scene machine
    /// clears the previous scene's pipeline, then registers the next scene's.
    /// Equivalent to `registerPipeline(event, {})`.
    void clearPipeline(IRTime::Events event);

    /// T-224: pipeline-groups API. Each inner vector is a "parallel
    /// group" of systems that the cross-system validator (see
    /// `validateAllPipelineGroups`) has cleared to co-execute on the
    /// worker pool. Groups themselves run sequentially in declaration
    /// order; `flushStructuralChanges` runs between groups, never
    /// between systems within a group (group members are required to
    /// be structurally clean by the validator). Replaces any prior
    /// registration for `event`.
    void registerPipelineGroups(IRTime::Events event, std::vector<std::vector<SystemId>> groups);

    /// #1540: append `system` to the END of `event`'s pipeline as its
    /// own singleton (serial) group, leaving every previously-registered
    /// group untouched. This is the composition primitive for a runtime
    /// where the C++ pipeline is built before a script runs (e.g. the
    /// midi runtime's `initSystems()` runs before `main.lua`): a Lua-
    /// authored system can join the live UPDATE/RENDER pipeline without
    /// re-listing — and double-creating — the existing C++ systems that
    /// `registerPipeline` / `registerPipelineGroups` would otherwise
    /// REPLACE. Creates the event's first group if none was registered
    /// yet. Asserts if `system` already appears in `event`'s pipeline (a
    /// second add would tick it twice per frame).
    void appendToPipeline(IRTime::Events event, SystemId system);

    /// #1540: insert `system` as its own singleton group immediately
    /// before / after the group that contains `anchor` in `event`'s
    /// pipeline. The position-aware sibling of `appendToPipeline` for
    /// when ordering relative to an existing system matters. Asserts if
    /// no pipeline is registered for `event`, `anchor` is absent, or
    /// `system` is already present.
    void insertIntoPipelineBefore(IRTime::Events event, SystemId system, SystemId anchor);
    void insertIntoPipelineAfter(IRTime::Events event, SystemId system, SystemId anchor);

    /// T-224: validate every registered pipeline group against the
    /// cross-system access rules in `system_access.hpp`. Call once
    /// after all systems and pipelines are registered, before the
    /// loop starts. FATALs on the first conflict found, naming both
    /// systems + the offending component. A group of size <= 1 is
    /// trivially clean and skipped.
    void validateAllPipelineGroups() const;

    void executePipeline(IRTime::Events event);
    void executeSystem(SystemId system);

    void setTimingEnabled(bool enabled) {
        m_timingEnabled = enabled;
    }
    bool isTimingEnabled() const {
        return m_timingEnabled;
    }
    void resetTimingStats();

    /// Take ownership of an observer; fires `onBeforeTick`/`onAfterTick`
    /// from the main thread around each singleton-group `executeSystem`
    /// call (see `TickObserver` for the multi-system parallel-group
    /// caveat). Returns an id usable with `unregisterTickObserver`. If
    /// `m_observers` is empty the dispatch is a single bool check, so
    /// unregistered systems pay nothing.
    TickObserverId registerTickObserver(std::unique_ptr<TickObserver> observer);
    void unregisterTickObserver(TickObserverId id);
    void clearTickObservers();

    const std::string &getSystemName(SystemId id) const {
        return m_systemNames[id].name_;
    }
    SystemId getSystemCount() const {
        return m_nextSystemId;
    }
    const TimingAccum &getTimingAccum(SystemId id) const {
        return m_timingAccum[id];
    }
    /// Flattened, declaration-order view of every pipeline's systems —
    /// preserved for callers that iterate pipelines for timing /
    /// telemetry (perf overlay, profile report) and don't care about
    /// the group partition. Groups are inlined in declaration order:
    /// `[[a,b], [c]]` reads as `[a, b, c]`. Built lazily from
    /// `m_systemPipelineGroups` on first read after a registration.
    const std::unordered_map<IRTime::Events, std::list<SystemId>> &getPipelines() const {
        refreshFlattenedPipelines();
        return m_flattenedPipelines;
    }

    /// T-224: read the group partition for `event` directly (one
    /// vector per parallel group, in declaration order). Empty if
    /// no pipeline was registered for `event`.
    const std::vector<std::vector<SystemId>> &getPipelineGroups(IRTime::Events event) const {
        static const std::vector<std::vector<SystemId>> kEmpty;
        auto it = m_systemPipelineGroups.find(event);
        return it == m_systemPipelineGroups.end() ? kEmpty : it->second;
    }

  private:
    SystemId m_nextSystemId = 0;
    std::vector<C_Name> m_systemNames;
    std::vector<C_SystemEvent<BEGIN_TICK>> m_beginTicks;
    std::vector<C_SystemEvent<TICK>> m_ticks;
    std::vector<C_SystemEvent<END_TICK>> m_endTicks;
    std::vector<C_SystemEvent<RELATION_TICK>> m_relationTicks;
    std::vector<C_SystemRelation> m_relations;
    std::vector<ErasedParamsPtr> m_systemParams;
    std::unordered_map<SystemName, SystemId> m_engineSystemIds;

    /// T-224: canonical pipeline storage is per-group. The legacy
    /// `registerPipeline(list<SystemId>)` translates each system into
    /// its own one-element group so existing call sites are
    /// unchanged.
    std::unordered_map<IRTime::Events, std::vector<std::vector<SystemId>>> m_systemPipelineGroups;

    /// Lazy mirror for `getPipelines()`'s flattened view. Rebuilt by
    /// `refreshFlattenedPipelines` after any group registration; the
    /// flag tracks whether the mirror is current.
    mutable std::unordered_map<IRTime::Events, std::list<SystemId>> m_flattenedPipelines;
    mutable bool m_flattenedPipelinesDirty{true};

    void refreshFlattenedPipelines() const;

    /// Shared impl for `insertIntoPipelineBefore` / `insertIntoPipelineAfter`.
    /// `after == false` inserts the new singleton group at the anchor's
    /// group index; `after == true` inserts immediately past it.
    void insertSingletonGroupRelativeTo(
        IRTime::Events event, SystemId system, SystemId anchor, bool after
    );

    bool m_timingEnabled = false;
    std::vector<TimingAccum> m_timingAccum;

    std::uint32_t m_nextObserverId = 1;
    std::vector<std::pair<TickObserverId, std::unique_ptr<TickObserver>>> m_observers;

    // T-222: per-system concurrency policy + grain size + access
    // descriptor. Parallel to m_ticks; emplaced in createSystem,
    // defaulted (SERIAL / kDefaultGrainSize / empty) for
    // createSystemDynamic.
    std::vector<Concurrency> m_concurrency;
    std::vector<int> m_grainSize;
    std::vector<SystemAccess> m_systemAccess;

    // #2404: per-system update cadence. Parallel to m_ticks; emplaced in
    // both system-creation paths via emplaceCadenceState.
    //   m_cadence         run 1-in-N phase ticks (1 = every tick).
    //   m_cadenceOffset   initial phase stagger, 0..cadence-1.
    //   m_lastRunTick     per-event tick counter at this system's last run,
    //                     seeded at pipeline-join (stampCadenceJoin) so the
    //                     first accumulated delta measures from the join
    //                     point, not counter zero.
    //   m_accumulatedTicks  phase ticks covered by the current/most-recent
    //                     execution (getAccumulatedTicks).
    std::vector<std::uint32_t> m_cadence;
    std::vector<std::uint32_t> m_cadenceOffset;
    std::vector<std::uint64_t> m_lastRunTick;
    std::vector<std::uint64_t> m_accumulatedTicks;

    // #2404: SystemManager-owned per-event execution counter, bumped once
    // at the top of executePipeline so every cadence gate in a pass sees
    // the same `now`. Self-contained (no TimeManager dependency), so
    // cadence works for INPUT/RENDER phases and is unit-testable in
    // isolation. IRTime::Events is a C-style enum ending at END; size the
    // array END + 1.
    std::array<std::uint64_t, IRTime::END + 1> m_eventTickCounts{};

    // #2404: reused scratch for filtering the due members of a multi-
    // system group on the main thread before fan-out; reserved lazily,
    // never reallocated per frame in steady state.
    std::vector<SystemId> m_dueScratch;

    // #2404: emplace the four cadence vectors for a newly-created system.
    // Called by both createSystem and createSystemDynamic to keep the
    // parallel vectors in lockstep with m_ticks. cadence 0 -> 1; offset is
    // clamped into [0, cadence-1].
    void emplaceCadenceState(std::uint32_t cadence, std::uint32_t offset);

    // #2404: main-thread cadence gate. Returns true and advances the
    // bookkeeping (accumulated ticks + last-run tick) when `system` is due
    // to run at phase tick `now`; returns false to skip the whole dispatch.
    bool pollCadenceDue(SystemId system, std::uint64_t now);

    // #2404: seed a system's phase when it joins `event`'s pipeline, so its
    // first accumulated delta measures from the join tick (plus its offset
    // stagger), not from counter zero.
    void stampCadenceJoin(IRTime::Events event, SystemId system);

    // #2404: the largest per-event tick counter — the reference clock for a
    // runtime offset re-phase (which is event-agnostic).
    std::uint64_t maxEventTickCount() const;

    // Begin tick functions happen once per system before tick function(s)
    template <typename FunctionBeginTick>
    void insertBeginTickFunction(FunctionBeginTick functionBeginTick) {
        if constexpr (std::is_invocable_v<FunctionBeginTick>) {
            m_beginTicks.emplace_back(C_SystemEvent<BEGIN_TICK>{[functionBeginTick]() {
                functionBeginTick();
            }});
        } else {
            m_beginTicks.emplace_back(C_SystemEvent<BEGIN_TICK>{[]() { return; }});
        }
    }

    // Tick functions are operated on each entity in the system
    // matching the archetype. The runtime stores one dispatch slot per
    // system — the binder form established by T-222/T-333:
    //
    //   - `prepareRangedTick(node)` — main-thread binder. Resolves
    //     the per-component vector refs from `node` once via
    //     `getComponentData<>` (which touches the EntityManager's
    //     hash map — unsafe under concurrent reads from workers),
    //     and returns a `void(rangeBegin, rangeEnd)` worker closure
    //     that captures those refs by value. Populated for every
    //     signature that iterates entities row-by-row (per-component,
    //     per-entity-id, optional-relations). `executeSystem` calls
    //     the binder once and invokes the returned closure with
    //     `[0, length)` for SERIAL/MAIN_THREAD, or fans the closure
    //     out to worker chunks via `IRJob::parallelFor` for
    //     PARALLEL_FOR. No per-node heap allocation.
    //   - `functionTick(node)` — used ONLY by the per-archetype batch
    //     form (`InvocableWithNodeVectors`) and dynamic systems
    //     (`createSystemDynamic` / Lua hot-reload). Row-iterating forms
    //     leave this empty and dispatch entirely through the binder.
    //
    // `Concurrency::PARALLEL_FOR` requires `prepareRangedTick` to be
    // populated; the entry-point validator in `ir_system.hpp` rejects
    // batch-form + PARALLEL_FOR.
    template <typename... Components, typename... RelationComponents, typename FunctionTick>
    void insertTickFunction(
        FunctionTick functionTick,
        RelationParams<RelationComponents...> extraParams,
        Archetype excludeArchetype
    ) {
        if constexpr (InvocableWithNodeVectors<FunctionTick, Components...>) {
            // Per-archetype batch form: the body sees the whole column,
            // never row-by-row. No ranged variant — chunking would
            // require splitting the vector, which the body's contract
            // disallows.
            auto perNodeFn = [functionTick](ArchetypeNode *node) {
                auto paramTuple = std::make_tuple(
                    node->type_,
                    std::ref(node->entities_),
                    std::ref(getComponentData<Components>(node))...
                );
                std::apply(
                    [&functionTick](auto &&...args) {
                        functionTick(std::forward<decltype(args)>(args)...);
                    },
                    paramTuple
                );
            };
            m_ticks.emplace_back(
                C_SystemEvent<TICK>{
                    std::function<void(ArchetypeNode *)>{std::move(perNodeFn)},
                    std::function<std::function<void(int, int)>(ArchetypeNode *)>{},
                    getArchetype<Components...>(),
                    std::move(excludeArchetype)
                }
            );
            return;
        } else {

            // Row-iterating forms: build a main-thread binder
            // (`prepareRangedTick`) that resolves the per-component
            // vector refs from the node once via `getComponentData<>` —
            // which goes through `EntityManager::m_pureComponentTypes`,
            // unsafe under concurrent reads — and returns a
            // `void(rangeBegin, rangeEnd)` worker closure that captures
            // those refs. The SERIAL / MAIN_THREAD path in `executeSystem`
            // calls the binder once and invokes the returned closure with
            // `[0, length)`; the PARALLEL_FOR dispatcher in `executeSystem`
            // calls the binder once and fans the closure out to worker
            // chunks via `IRJob::parallelFor`. Wrapped in
            // an `else` so the unsupported-signature static_assert below
            // only fires when the batch-form branch above has NOT
            // discarded this code.
            auto prepareRangedTick =
                [functionTick, extraParams](ArchetypeNode *node) -> std::function<void(int, int)> {
                if constexpr (InvocableWithEntityId<FunctionTick, Components...>) {
                    auto componentsTuple = std::make_tuple(
                        std::ref(node->entities_),
                        std::ref(getComponentData<Components>(node))...
                    );
                    return [functionTick, componentsTuple](int rangeBegin, int rangeEnd) {
                        for (int i = rangeBegin; i < rangeEnd; i++) {
                            std::apply(
                                [i, &functionTick](auto &&...components) {
                                    functionTick(components[i]...);
                                },
                                componentsTuple
                            );
                        }
                    };
                } else if constexpr (InvocableWithComponents<FunctionTick, Components...>) {
                    auto componentsTuple =
                        std::make_tuple(std::ref(getComponentData<Components>(node))...);
                    return [functionTick, componentsTuple](int rangeBegin, int rangeEnd) {
                        for (int i = rangeBegin; i < rangeEnd; i++) {
                            std::apply(
                                [i, &functionTick](auto &&...components) {
                                    functionTick(components[i]...);
                                },
                                componentsTuple
                            );
                        }
                    };
                } else if constexpr (
                    std::is_invocable_v<
                        FunctionTick,
                        Components &...,
                        std::optional<RelationComponents *>...>
                ) {
                    // Relation form: the validator (T-334 scope) rejects
                    // PARALLEL_FOR + relation, so this binder runs only
                    // on the main thread. Resolve component vectors AND
                    // the optional-relation pointers up-front; the
                    // returned closure iterates rows over both.
                    auto componentsTuple =
                        std::make_tuple(std::ref(getComponentData<Components>(node))...);
                    EntityId relatedEntity =
                        getRelatedEntityFromArchetype(node->type_, extraParams.relation_);
                    auto relationComponentTuple =
                        std::make_tuple(getComponentOptional<RelationComponents>(relatedEntity)...);
                    return [functionTick,
                            componentsTuple,
                            relationComponentTuple](int rangeBegin, int rangeEnd) {
                        for (int i = rangeBegin; i < rangeEnd; i++) {
                            std::apply(
                                [&functionTick](auto &&...args) { functionTick(args...); },
                                std::tuple_cat(
                                    std::make_tuple(
                                        std::ref(
                                            std::get<std::vector<Components> &>(componentsTuple)[i]
                                        )...
                                    ),
                                    relationComponentTuple
                                )
                            );
                        }
                    };
                } else {
                    static_assert(false, "Unsupported tick function signature.");
                }
            };
            m_ticks.emplace_back(
                C_SystemEvent<TICK>{
                    std::function<void(ArchetypeNode *)>{},
                    C_SystemEvent<TICK>::PrepareRangedTickFn{std::move(prepareRangedTick)},
                    getArchetype<Components...>(),
                    std::move(excludeArchetype)
                }
            );
        }
    }

    // End tick functions happen once per system after tick function(s)
    template <typename FunctionEndTick>
    void insertEndTickFunction(FunctionEndTick functionEndTick) {
        if constexpr (std::is_invocable_v<FunctionEndTick>) {
            m_endTicks.emplace_back(C_SystemEvent<END_TICK>{[functionEndTick]() {
                functionEndTick();
            }});
        } else {
            m_endTicks.emplace_back(C_SystemEvent<END_TICK>{[]() { return; }});
        }
    }

    // Relation tick functions execute once per related entity
    // before the tick function on its children entities.
    template <typename... RelationComponents, typename FunctionRelationTick>
    void insertRelationTickFunction(FunctionRelationTick functionRelationTick) {
        if constexpr (std::is_invocable_v<FunctionRelationTick, RelationComponents &...>) {
            m_relationTicks.emplace_back(
                C_SystemEvent<RELATION_TICK>{[functionRelationTick](EntityRecord entityRecord) {
                    auto componentsTuple = std::make_tuple(
                        std::ref(
                            getComponentData<RelationComponents>(
                                entityRecord.archetypeNode
                            )[entityRecord.row]
                        )...
                    );
                    std::apply(
                        [functionRelationTick](auto &&...components) {
                            functionRelationTick(components...);
                        },
                        componentsTuple
                    );
                }}
            );
        } else if constexpr (std::is_same_v<FunctionRelationTick, std::nullptr_t>) {
            m_relationTicks.emplace_back(C_SystemEvent<RELATION_TICK>{[](EntityRecord record) {
                return;
            }});
        } else {
            static_assert(false, "Unsupported relation tick function signature.");
        }
    }

    EntityId handleRelationTick(
        ArchetypeNode *currentNode, SystemId currentSystem, EntityId previousRelatedEntity
    );
};

} // namespace IRSystem

#endif /* SYSTEM_MANAGER_H */
