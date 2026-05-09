#include <irreden/ir_system.hpp>
#include <irreden/system/system_manager.hpp>

#include <irreden/common/components/component_name.hpp>

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
    std::function<void(ArchetypeNode *)> body
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
    for (auto node : nodes) {
        if (m_timingEnabled) {
            entityCount += static_cast<uint64_t>(node->length_);
        }
        previousRelatedEntity = handleRelationTick(node, system, previousRelatedEntity);
        m_ticks[system].functionTick_(node);
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
