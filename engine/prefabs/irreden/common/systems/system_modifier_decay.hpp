#ifndef SYSTEM_MODIFIER_DECAY_H
#define SYSTEM_MODIFIER_DECAY_H

// Decrements ticksRemaining_ on every per-entity Modifier and drops
// expired entries. Runs at the start of the modifier pipeline, before
// any RESOLVE system. Because decay precedes all RESOLVE systems, a
// modifier with ticksRemaining_=1 is removed before its first resolve
// pass — it fires for zero frames. Use ticksRemaining_=2 to fire for
// exactly one frame.
//
// `ticksRemaining_ == -1` is the sentinel for "no decay" — those entries
// are kept untouched.
//
// A pass reads the framework's decay flag (see `FieldRegistry::armDecay`)
// in `beginTick` and skips every row while it is clear; a pass that leaves
// no finite lifetime behind clears it. Assert builds still visit the rows
// of a skipped pass, read-only, and fail in `endTick` if one holds a finite
// lifetime that no push armed. A bare `tick` call without `beginTick`
// always decays.

#include <irreden/ir_job.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/modifier_field_registry.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace IRSystem {

template <> struct System<MODIFIER_DECAY> {
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    bool decay_ = true;
    bool passOpen_ = false;
    bool decayedLastPass_ = false;
    // One slot per executor (main thread plus workers), indexed by
    // `IRJob::workerId()`. `finiteKept_` marks a row that still holds a
    // finite lifetime after decay; `unarmedFinite_` marks a finite lifetime
    // found by the assert-build audit of a skipped pass.
    std::vector<std::uint8_t> finiteKept_;
    std::vector<std::uint8_t> unarmedFinite_;

    void beginTick() {
        decay_ = IRPrefab::Modifier::detail::globalFieldRegistry().decayArmed();
        const std::size_t slots = static_cast<std::size_t>(IRJob::workerCount()) + 1u;
        finiteKept_.assign(slots, 0u);
        unarmedFinite_.assign(slots, 0u);
        passOpen_ = true;
    }

    void tick(IRComponents::C_Modifiers &m) {
        if (!decay_) {
#ifndef IR_RELEASE
            if (holdsFinite(m)) {
                mark(unarmedFinite_);
            }
#endif
            return;
        }
        const bool kept =
            decayRow(m.modifiers_) | decayRow(m.modifiersVec3_) | decayRow(m.modifiersQuat_);
        if (kept && passOpen_) {
            mark(finiteKept_);
        }
    }

    void endTick() {
        auto &registry = IRPrefab::Modifier::detail::globalFieldRegistry();
        const bool decayed = decay_;
        decayedLastPass_ = decayed;
        decay_ = true;
        passOpen_ = false;
        if (decayed) {
            if (!any(finiteKept_)) {
                registry.clearDecay();
            }
            return;
        }
        if (any(unarmedFinite_)) {
            // Arm before failing so the next pass decays the stray modifier.
            registry.armDecay();
            IR_ASSERT(
                false,
                "MODIFIER_DECAY: a C_Modifiers holds a finite-lifetime modifier no push armed "
                "the decay for; give per-entity modifiers a lifetime only through "
                "IRPrefab::Modifier::push, pushFrameLocal or pushOneFrame"
            );
        }
    }

    static SystemId create() {
        return registerSystem<MODIFIER_DECAY, IRComponents::C_Modifiers>("ModifierDecay");
    }

  private:
    // Decays one vector; returns whether a finite lifetime survives it.
    template <typename T> static bool decayRow(std::vector<T> &v) {
        bool kept = false;
        auto newEnd = std::remove_if(v.begin(), v.end(), [&kept](T &mod) {
            const bool expired = IRComponents::detail::tickAndExpired(mod);
            kept |= !expired && mod.ticksRemaining_ != -1;
            return expired;
        });
        v.erase(newEnd, v.end());
        return kept;
    }

    static bool holdsFinite(const IRComponents::C_Modifiers &m) {
        const auto finite = [](const auto &mod) { return mod.ticksRemaining_ != -1; };
        return std::any_of(m.modifiers_.begin(), m.modifiers_.end(), finite) ||
               std::any_of(m.modifiersVec3_.begin(), m.modifiersVec3_.end(), finite) ||
               std::any_of(m.modifiersQuat_.begin(), m.modifiersQuat_.end(), finite);
    }

    static void mark(std::vector<std::uint8_t> &slots) {
        const auto worker = static_cast<std::size_t>(IRJob::workerId());
        if (worker < slots.size() && slots[worker] == 0u) {
            slots[worker] = 1u;
        }
    }

    static bool any(const std::vector<std::uint8_t> &slots) {
        return std::any_of(slots.begin(), slots.end(), [](std::uint8_t s) { return s != 0u; });
    }
};

} // namespace IRSystem

#endif /* SYSTEM_MODIFIER_DECAY_H */
