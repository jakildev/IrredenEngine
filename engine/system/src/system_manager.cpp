#include <irreden/ir_system.hpp>
#include <irreden/system/system_manager.hpp>

#include <irreden/common/components/component_name.hpp>
#include <irreden/ir_job.hpp>

#include <chrono>

namespace IRSystem {

SystemManager::SystemManager()
    : m_nextSystemId{0} {
    g_systemManager = this;
    IRE_LOG_INFO("Created SystemManager");
}

SystemManager::~SystemManager() {
    if (g_systemManager == this) {
        g_systemManager = nullptr;
    }
}

void SystemManager::resetTimingStats() {
    for (auto &acc : m_timingAccum) {
        acc = TimingAccum{};
    }
}

TickObserverId SystemManager::registerTickObserver(std::unique_ptr<TickObserver> observer) {
    TickObserverId id{m_nextObserverId++};
    m_observers.emplace_back(id, std::move(observer));
    return id;
}

void SystemManager::unregisterTickObserver(TickObserverId id) {
    for (auto it = m_observers.begin(); it != m_observers.end(); ++it) {
        if (it->first.value_ == id.value_) {
            m_observers.erase(it);
            return;
        }
    }
}

void SystemManager::clearTickObservers() {
    m_observers.clear();
}

SystemId SystemManager::createSystemDynamic(
    std::string name,
    Archetype includeArchetype,
    Archetype excludeArchetype,
    std::function<void(ArchetypeNode *)> body,
    Concurrency concurrency
) {
    m_systemNames.emplace_back(C_Name{std::move(name)});
    SystemId newSystemId = m_nextSystemId++;

    m_beginTicks.emplace_back(C_SystemEvent<BEGIN_TICK>{[]() {}});
    m_ticks.emplace_back(C_SystemEvent<TICK>{
        std::move(body),
        std::move(includeArchetype),
        std::move(excludeArchetype),
    });
    m_endTicks.emplace_back(C_SystemEvent<END_TICK>{[]() {}});
    m_relationTicks.emplace_back(C_SystemEvent<RELATION_TICK>{[](EntityRecord) {}});

    m_relations.emplace_back(C_SystemRelation{Relation::NONE});
    m_systemParams.emplace_back(nullptr);
    m_timingAccum.emplace_back();
    // T-222: dynamic systems are PARALLEL_FOR-ineligible (the body is
    // opaque to row-level chunking — `m_ticks[…].prepareRangedTick_` is
    // never populated). T-223 still accepts SERIAL / MAIN_THREAD so the
    // Lua surface can tag EVAL systems as MAIN_THREAD to opt them out of
    // pipeline-group parallelism (the sol2 / LuaJIT GC singletons are
    // not thread-safe). PARALLEL_FOR is normalized to MAIN_THREAD by the
    // Lua-side `IRSystem.registerSystem` shim before reaching here; the
    // assert is a defence in depth for any future direct C++ caller.
    IR_ASSERT(
        concurrency != Concurrency::PARALLEL_FOR,
        "createSystemDynamic: PARALLEL_FOR not supported for runtime-typed "
        "systems (no row-level ranged tick). Use SERIAL or MAIN_THREAD."
    );
    m_concurrency.emplace_back(concurrency);
    m_grainSize.emplace_back(kDefaultGrainSize);
    m_systemAccess.emplace_back();
    return newSystemId;
}

void SystemManager::replaceSystemBody(SystemId system, std::function<void(ArchetypeNode *)> body) {
    IR_ASSERT(
        system < m_nextSystemId,
        "replaceSystemBody: SystemId {} out of range (have {} systems)",
        system,
        m_nextSystemId
    );
    IR_ASSERT(static_cast<bool>(body), "replaceSystemBody: body must be a non-empty std::function");
    m_ticks[system].functionTick_ = std::move(body);
}

void SystemManager::registerPipeline(IRTime::Events event, std::list<SystemId> pipeline) {
    // T-224: legacy single-list form becomes a per-system group
    // sequence. Each system runs as its own one-element group, which
    // dispatches serially through the same code path as before —
    // bit-for-bit equivalent to the previous behavior for existing
    // call sites.
    std::vector<std::vector<SystemId>> groups;
    groups.reserve(pipeline.size());
    for (SystemId s : pipeline) {
        groups.push_back({s});
    }
    registerPipelineGroups(event, std::move(groups));
}

void SystemManager::registerPipelineGroups(
    IRTime::Events event, std::vector<std::vector<SystemId>> groups
) {
    m_systemPipelineGroups[event] = std::move(groups);
    m_flattenedPipelinesDirty = true;
}

void SystemManager::refreshFlattenedPipelines() const {
    if (!m_flattenedPipelinesDirty) {
        return;
    }
    m_flattenedPipelines.clear();
    for (const auto &[event, groups] : m_systemPipelineGroups) {
        std::list<SystemId> flat;
        for (const auto &group : groups) {
            for (SystemId id : group) {
                flat.push_back(id);
            }
        }
        m_flattenedPipelines.emplace(event, std::move(flat));
    }
    m_flattenedPipelinesDirty = false;
}

void SystemManager::validateAllPipelineGroups() const {
    for (const auto &[event, groups] : m_systemPipelineGroups) {
        for (std::size_t gi = 0; gi < groups.size(); ++gi) {
            const auto &group = groups[gi];
            if (group.size() <= 1) {
                continue;
            }
            // Reject PARALLEL_FOR members in multi-system groups: the inner
            // IRJob::parallelFor they drive cannot fan out from a worker
            // thread (main-thread assert, potential deadlock under full-pool
            // saturation). Each PARALLEL_FOR system must run in its own
            // singleton group.
            for (std::size_t mi = 0; mi < group.size(); ++mi) {
                const SystemId mid = group[mi];
                if (mid < m_concurrency.size() && m_concurrency[mid] == Concurrency::PARALLEL_FOR) {
                    IR_ASSERT(
                        false,
                        "registerPipelineGroups: system '{}' (group {}) has "
                        "Concurrency::PARALLEL_FOR and cannot share a parallel group "
                        "with siblings — its inner IRJob::parallelFor cannot fan out "
                        "from a worker thread. Move it to its own singleton group.",
                        getSystemName(mid),
                        gi
                    );
                }
            }
            std::vector<SystemAccess> accesses;
            accesses.reserve(group.size());
            for (SystemId id : group) {
                accesses.push_back(getSystemAccess(id));
            }
            GroupConflict c = findPipelineGroupConflict(accesses.data(), accesses.size());
            if (c.kind_ == GroupConflictKind::NONE) {
                continue;
            }
            const std::string &nameA = getSystemName(group[c.indexA_]);
            const std::string &nameB = getSystemName(group[c.indexB_]);
            switch (c.kind_) {
            case GroupConflictKind::MAIN_THREAD_IN_GROUP:
                IR_ASSERT(
                    false,
                    "registerPipelineGroups: system '{}' (group {}) carries the "
                    "IRSystem::MainThread tag and cannot share a parallel group "
                    "with another system. Move it to its own group.",
                    nameA,
                    gi
                );
                break;
            case GroupConflictKind::TWO_SPAWNERS:
                // T-225: validator no longer produces this kind —
                // per-worker deferred-mutation buffers cover
                // concurrent archetype-graph mutation. Kept as an
                // exhaustive-switch sentinel; unreachable in practice.
                break;
            case GroupConflictKind::MUTATOR_IN_PARALLEL_GROUP:
                // T-225 lifted this restriction — per-worker deferred-mutation
                // buffers make mutators safe in parallel groups.
                // Kept as exhaustive-switch sentinel; unreachable in practice.
                break;
            case GroupConflictKind::WRITE_WRITE:
                IR_ASSERT(
                    false,
                    "registerPipelineGroups: systems '{}' and '{}' (group {}) both "
                    "write the same component column. Concurrent writes are a "
                    "data race; split them across groups.",
                    nameA,
                    nameB,
                    gi
                );
                break;
            case GroupConflictKind::WRITE_READ:
                IR_ASSERT(
                    false,
                    "registerPipelineGroups: system '{}' writes a component that "
                    "system '{}' reads (group {}). The reader would observe a "
                    "torn snapshot; split them across groups.",
                    nameA,
                    nameB,
                    gi
                );
                break;
            case GroupConflictKind::READ_WRITE:
                IR_ASSERT(
                    false,
                    "registerPipelineGroups: system '{}' reads a component that "
                    "system '{}' writes (group {}). The reader would observe a "
                    "torn snapshot; split them across groups.",
                    nameA,
                    nameB,
                    gi
                );
                break;
            case GroupConflictKind::NONE:
                break;
            }
        }
    }
}

void SystemManager::executePipeline(IRTime::Events event) {
    IREntity::flushStructuralChanges();
    auto it = m_systemPipelineGroups.find(event);
    if (it == m_systemPipelineGroups.end()) {
        return;
    }
    const auto &groups = it->second;
    for (const auto &group : groups) {
        if (group.size() == 1) {
            // T-224: observer bracket fires on the main thread around
            // each singleton system — the only group shape that
            // preserves per-system timing semantics. The bracket lives
            // here (not in executeSystem) because executeSystem is
            // reached from worker threads on multi-system groups, and
            // the engine's GpuStageTimingObserver writes shared state
            // + drives main-thread-only GPU APIs (device()->finish(),
            // writeTimestamp). See registerTickObserver doc + the
            // multi-system note below.
            const SystemId id = group[0];
            for (auto &entry : m_observers) {
                entry.second->onBeforeTick(id);
            }
            executeSystem(id);
            for (auto &entry : m_observers) {
                entry.second->onAfterTick(id);
            }
        } else if (!group.empty()) {
            // T-224: parallel group — fan out across the worker pool.
            // grainSize=1 makes each task one system; the validator
            // already guarantees no two members conflict.
            //
            // Observer bracketing is intentionally skipped for
            // multi-system parallel groups: per-system timing is
            // meaningless when the systems run concurrently (their
            // wall-time windows overlap), and the GpuStageTimingObserver
            // writes shared per-stage state from main-thread-only GPU
            // APIs. The validator already rejects MAIN_THREAD-tagged
            // members, so any caller that needs per-system observer
            // brackets must put the observed system in its own
            // singleton group.
            if (g_jobManager != nullptr) {
                IRJob::parallelFor(
                    0,
                    static_cast<int>(group.size()),
                    1,
                    [&group, this](int begin, int end) {
                        for (int k = begin; k < end; ++k) {
                            executeSystem(group[k]);
                        }
                    }
                );
            } else {
                // No worker pool (unit tests, pre-World init) — fall
                // back to serial dispatch in declaration order so the
                // pipeline still runs.
                for (SystemId id : group) {
                    executeSystem(id);
                }
            }
        }
        IREntity::flushStructuralChanges();
    }
}

void SystemManager::executeSystem(SystemId system) {
    // Reachable from a worker thread when called inside a multi-system
    // parallel group dispatch (executePipeline's IRJob::parallelFor
    // body). The CPU profile block and the per-system Clock::now()
    // pair are thread-safe (easy_profiler partitions per-thread; the
    // m_timingAccum write is per-SystemId and only one worker handles
    // any given SystemId per group), so the only mutations that need
    // a thread context are the observer fires (moved to executePipeline)
    // and the nested IRJob::parallelFor below (guarded).
    IR_PROFILE_BLOCK(m_systemNames[system].name_.c_str(), IR_PROFILER_COLOR_SYSTEMS);

    using Clock = std::chrono::steady_clock;
    Clock::time_point t0;
    if (m_timingEnabled) {
        t0 = Clock::now();
    }

    m_beginTicks[system].functionBeginTick_();
    std::vector<ArchetypeNode *> nodes;
    if (m_relations[system].relation_ == Relation::NONE) {
        nodes = IREntity::queryArchetypeNodesSimple(
            m_ticks[system].archetype_,
            m_ticks[system].excludeArchetype_
        );
    }
    if (m_relations[system].relation_ == Relation::CHILD_OF) {
        nodes = IREntity::queryArchetypeNodesRelational(
            m_relations[system].relation_,
            m_ticks[system].archetype_,
            m_ticks[system].excludeArchetype_
        );
    }

    uint64_t entityCount = 0;
    EntityId previousRelatedEntity = kNullEntity;
    const Concurrency concurrency =
        system < m_concurrency.size() ? m_concurrency[system] : Concurrency::SERIAL;
    const int grainSize = (system < m_grainSize.size() && m_grainSize[system] > 0)
                              ? m_grainSize[system]
                              : kDefaultGrainSize;
    const auto &tickEvent = m_ticks[system];

    for (auto node : nodes) {
        if (m_timingEnabled) {
            entityCount += static_cast<uint64_t>(node->length_);
        }
        previousRelatedEntity = handleRelationTick(node, system, previousRelatedEntity);

        if (concurrency == Concurrency::PARALLEL_FOR &&
            static_cast<bool>(tickEvent.prepareRangedTick_) && node->length_ > grainSize &&
            g_jobManager != nullptr && IRJob::isMainThread()) {
            // Worker fan-out: split [0, length) into grainSize chunks
            // and run each on the pool. enkiTS' WaitforTask pumps the
            // main thread, so this blocks until every chunk finishes.
            //
            // T-333: the binder resolves all per-component vector refs
            // here on the main thread before any worker is spawned, so
            // `EntityManager::m_pureComponentTypes`-touching code (the
            // `getComponentType<>` hash lookup hidden inside
            // `getComponentData<>`) never runs concurrently from
            // workers. Workers only iterate captured refs.
            //
            // `isMainThread()` (T-224) guards against nested dispatch:
            // when executeSystem is reached from a worker (multi-system
            // parallel group), IRJob::parallelFor would FATAL on its
            // own main-thread assert. validateAllPipelineGroups rejects
            // PARALLEL_FOR members in multi-system groups at boot, so
            // this path is unreachable in well-formed programs; the
            // guard is kept as a release-build safety net.
            auto rangedTick = tickEvent.prepareRangedTick_(node);
            IRJob::parallelFor(
                0,
                node->length_,
                grainSize,
                [&rangedTick](int rangeBegin, int rangeEnd) { rangedTick(rangeBegin, rangeEnd); }
            );
        } else {
            // SERIAL / MAIN_THREAD path, the small-node / no-pool
            // fallback for PARALLEL_FOR, and the nested-dispatch
            // fallback when executeSystem is called from a worker.
            // Row-iterating forms have prepareRangedTick_ only; batch
            // and dynamic forms have functionTick_ only (T-348).
            if (static_cast<bool>(tickEvent.prepareRangedTick_)) {
                auto rangedTick = tickEvent.prepareRangedTick_(node);
                rangedTick(0, node->length_);
            } else {
                tickEvent.functionTick_(node);
            }
        }
    }
    m_endTicks[system].functionEndTick_();

    if (m_timingEnabled) {
        auto elapsed = Clock::now() - t0;
        uint64_t ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()
        );
        auto &acc = m_timingAccum[system];
        acc.totalNs_ += ns;
        if (ns < acc.minNs_)
            acc.minNs_ = ns;
        if (ns > acc.maxNs_)
            acc.maxNs_ = ns;
        acc.callCount_++;
        acc.totalEntityCount_ += entityCount;
    }

    // T-224: observer fires moved to executePipeline (singleton groups
    // only) — they need a main-thread context that executeSystem can
    // no longer guarantee. structural-changes flush also runs there,
    // per-group rather than per-system.
}

EntityId SystemManager::handleRelationTick(
    ArchetypeNode *currentNode, SystemId currentSystem, EntityId previousRelatedEntity
) {
    EntityId relatedEntity =
        getRelatedEntityFromArchetype(currentNode->type_, m_relations[currentSystem].relation_);
    if (relatedEntity != previousRelatedEntity && relatedEntity != kNullEntity) {
        m_relationTicks[currentSystem].functionRelationTick_(getEntityRecord(relatedEntity));
    }
    return relatedEntity;
}

} // namespace IRSystem
