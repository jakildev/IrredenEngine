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

#include <chrono>
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

/// Observer hook fired before and after every system tick. Used by the
/// render layer to bracket GPU-stage timing samples around per-system work
/// without inlining `device()->finish()` blocks into every tick lambda.
/// Generic on purpose: trace capture or per-system telemetry can plug in
/// using the same hook without touching SystemManager again.
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
        std::function<void(ArchetypeNode *)> body
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
    /// around every `executeSystem` call. Returns an id usable with
    /// `unregisterTickObserver`. If `m_observers` is empty the dispatch
    /// is a single bool check, so unregistered systems pay nothing.
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
    const std::unordered_map<IRTime::Events, std::list<SystemId>> &getPipelines() const {
        return m_systemPipelinesNew;
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

    std::unordered_map<IRTime::Events, std::list<SystemId>> m_systemPipelinesNew;

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
    // matching the archetype. The runtime stores **two** dispatch
    // shapes per system (T-222 Phase 2 of the multithreading epic):
    //
    //   - `rangedTick(node, begin, end)` — invokes the body across
    //     `[begin, end)`. Populated for every signature that iterates
    //     entities row-by-row (per-component, per-entity-id,
    //     optional-relations). The per-archetype batch form
    //     (`InvocableWithNodeVectors`) consumes the whole column and
    //     is intentionally unchunkable — `rangedTick` is empty in that
    //     case; the per-node `functionTick_` does the dispatch.
    //   - `functionTick(node)` — back-compat per-node entry. For the
    //     row-iterating forms it's a thin `rangedTick(node, 0, length)`
    //     wrapper; for the batch form it carries the body directly.
    //
    // `Concurrency::PARALLEL_FOR` requires `rangedTick` to be
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
                    std::function<void(ArchetypeNode *, int, int)>{},
                    getArchetype<Components...>(),
                    std::move(excludeArchetype)
                }
            );
            return;
        } else {

            // Row-iterating forms: build the range-aware lambda once,
            // synthesize the per-node entry as a thin wrapper that calls
            // it with `[0, length)`. Wrapped in an `else` so the
            // unsupported-signature static_assert below only fires when
            // the batch-form branch above has NOT discarded this code.
            auto rangedFn = [functionTick,
                             extraParams](ArchetypeNode *node, int rangeBegin, int rangeEnd) {
                if constexpr (InvocableWithEntityId<FunctionTick, Components...>) {
                    auto componentsTuple = std::make_tuple(
                        std::ref(node->entities_),
                        std::ref(getComponentData<Components>(node))...
                    );
                    for (int i = rangeBegin; i < rangeEnd; i++) {
                        std::apply(
                            [i, &functionTick](auto &&...components) {
                                functionTick(components[i]...);
                            },
                            componentsTuple
                        );
                    }
                } else if constexpr (InvocableWithComponents<FunctionTick, Components...>) {
                    auto componentsTuple =
                        std::make_tuple(std::ref(getComponentData<Components>(node))...);
                    for (int i = rangeBegin; i < rangeEnd; i++) {
                        std::apply(
                            [i, &functionTick](auto &&...components) {
                                functionTick(components[i]...);
                            },
                            componentsTuple
                        );
                    }
                } else if constexpr (
                    std::is_invocable_v<
                        FunctionTick,
                        Components &...,
                        std::optional<RelationComponents *>...>
                ) {
                    auto componentsTuple =
                        std::make_tuple(std::ref(getComponentData<Components>(node))...);
                    EntityId relatedEntity =
                        getRelatedEntityFromArchetype(node->type_, extraParams.relation_);
                    auto relationComponentTuple =
                        std::make_tuple(getComponentOptional<RelationComponents>(relatedEntity)...);

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
                } else {
                    static_assert(false, "Unsupported tick function signature.");
                }
            };
            std::function<void(ArchetypeNode *, int, int)> rangedFnErased{std::move(rangedFn)};
            auto rangedFnCopy = rangedFnErased;
            auto perNodeFn = [rangedFnCopy = std::move(rangedFnCopy)](ArchetypeNode *node) {
                rangedFnCopy(node, 0, node->length_);
            };
            m_ticks.emplace_back(
                C_SystemEvent<TICK>{
                    std::function<void(ArchetypeNode *)>{std::move(perNodeFn)},
                    std::move(rangedFnErased),
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
