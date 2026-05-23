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
    m_ticks.emplace_back(
        C_SystemEvent<TICK>{
            std::move(body),
            std::move(includeArchetype),
            std::move(excludeArchetype),
        }
    );
    m_endTicks.emplace_back(C_SystemEvent<END_TICK>{[]() {}});
    m_relationTicks.emplace_back(C_SystemEvent<RELATION_TICK>{[](EntityRecord) {}});

    m_relations.emplace_back(C_SystemRelation{Relation::NONE});
    m_systemParams.emplace_back(nullptr);
    m_timingAccum.emplace_back();
    // T-222: dynamic systems are PARALLEL_FOR-ineligible (the body is
    // opaque to row-level chunking — `m_ticks[…].rangedFunctionTick_` is
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
    m_systemPipelinesNew[event] = pipeline;
}

void SystemManager::executePipeline(IRTime::Events event) {
    IREntity::flushStructuralChanges();
    auto &systemOrder = m_systemPipelinesNew[event];
    for (SystemId system : systemOrder) {
        executeSystem(system);
    }
    IREntity::flushStructuralChanges();
}

void SystemManager::executeSystem(SystemId system) {
    IR_PROFILE_BLOCK(m_systemNames[system].name_.c_str(), IR_PROFILER_COLOR_SYSTEMS);

    // Observer fires bracket the entire system execution. They sit outside
    // the CPU timing window so a GPU observer's `device()->finish()` doesn't
    // get billed against m_timingAccum (CPU vs GPU stay orthogonal).
    for (auto &entry : m_observers) {
        entry.second->onBeforeTick(system);
    }

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
            static_cast<bool>(tickEvent.rangedFunctionTick_) && node->length_ > grainSize &&
            g_jobManager != nullptr) {
            // Worker fan-out: split [0, length) into grainSize chunks
            // and run each on the pool. enkiTS' WaitforTask pumps the
            // main thread, so this blocks until every chunk finishes.
            const auto &rangedFn = tickEvent.rangedFunctionTick_;
            IRJob::parallelFor(
                0,
                node->length_,
                grainSize,
                [&rangedFn, node](int rangeBegin, int rangeEnd) {
                    rangedFn(node, rangeBegin, rangeEnd);
                }
            );
        } else {
            // SERIAL / MAIN_THREAD path, and the small-node /
            // no-pool fallback for PARALLEL_FOR.
            tickEvent.functionTick_(node);
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

    for (auto &entry : m_observers) {
        entry.second->onAfterTick(system);
    }

    IREntity::flushStructuralChanges();
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
