#ifndef COMPONENT_STATIC_STATE_H
#define COMPONENT_STATIC_STATE_H

#include <irreden/entity/ir_entity_types.hpp>

#include <cstdint>
#include <vector>

namespace IRComponents {

/// Lifetime counters of the static-resident transition step. Populations
/// are read live by `IRPrefab::Static::stats`.
struct StaticCounters {
    std::uint64_t promoted_ = 0;
    std::uint64_t thawed_ = 0;
    std::uint64_t resettled_ = 0;
    std::uint64_t restarted_ = 0;
    std::uint64_t held_ = 0;
    std::uint64_t heldByGlobal_ = 0;
    std::uint64_t refused_ = 0;
    double promotionMsMax_ = 0.0;
    double drainMsMax_ = 0.0;
};

/// Singleton, engine-owned: what `IRPrefab::Static::drain` carries between
/// calls. Only the drain writes it.
struct C_StaticState {
    /// No entity is promoted before a round that opened after the last
    /// global-modifier change has completed.
    std::uint64_t globalFloor_ = 0;
    std::uint64_t seenGlobalRevision_ = 0;
    /// The completed round the promotion budget was last charged against.
    std::uint64_t budgetRound_ = 0;
    std::uint64_t promotedThisRound_ = 0;
    /// The completed round last counted in `heldByGlobal_`.
    std::uint64_t heldByGlobalRound_ = 0;
    /// Next settled row the audit checks, as a running offset over the
    /// settled population.
    std::uint64_t auditCursor_ = 0;
    StaticCounters counters_;

    // Scratch reused across drains; capacity persists.
    struct Candidate {
        std::uint64_t stamp_;
        IREntity::EntityId entity_;
    };
    std::vector<IREntity::EntityId> notes_;
    std::vector<Candidate> candidates_;
    std::vector<IREntity::EntityId> refused_;
    // Modifier field ids already warned about in `warnGlobalChangeMissesSettled`.
    std::vector<std::uint16_t> warnedGlobalFields_;
};

} // namespace IRComponents

#endif /* COMPONENT_STATIC_STATE_H */
