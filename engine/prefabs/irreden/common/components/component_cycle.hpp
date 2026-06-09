#ifndef COMPONENT_CYCLE_H
#define COMPONENT_CYCLE_H

// C_Cycle — a generic tick-aligned recurring period. Many can coexist (one per
// "day", "season", "boss_phase", ...). SYSTEM_CYCLE_BOUNDARY_DETECT advances the
// resolver state each UPDATE tick and, on the tick a period boundary is crossed,
// raises the embedded boundary event (`boundaryCrossed_` + from/to cycle number).
//
// Boundary events ride on the component itself rather than a separate event
// component, matching the engine's events-as-components convention (cf.
// C_ContactEvent's entered_/exited_ flags): a consumer system ordered AFTER
// CYCLE_BOUNDARY_DETECT reads `boundaryCrossed_` the same tick it fires, and the
// detector recomputes it every tick so it is self-clearing (no separate clear
// system needed). For continuous time-driven values (sun angle, color temp),
// read IRSim::cycleFraction(name) instead of waiting for the discrete boundary.
//
// `lastCycleNum_` defaults to the unprimed sentinel so a cycle created at any
// sim tick (not just tick 0) does not fire a spurious boundary on its first
// evaluation — the detector primes it silently, then fires on real crossings.

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace IRComponents {

struct C_Cycle {
    static constexpr std::uint64_t kUnprimed = std::numeric_limits<std::uint64_t>::max();

    std::string name_;
    std::uint64_t periodTicks_ = 1;       // length of one cycle in sim ticks (>= 1)
    std::uint64_t phaseOffset_ = 0;       // shifts where the cycle boundary lands

    std::uint64_t lastCycleNum_ = kUnprimed; // resolver state; kUnprimed until first detect

    // Embedded boundary event (valid only on the tick boundaryCrossed_ is true).
    bool boundaryCrossed_ = false;
    std::uint64_t fromCycle_ = 0;
    std::uint64_t toCycle_ = 0;

    C_Cycle() = default;

    C_Cycle(std::string name, std::uint64_t periodTicks, std::uint64_t phaseOffset = 0)
        : name_{std::move(name)}
        , periodTicks_{periodTicks}
        , phaseOffset_{phaseOffset} {}
};

} // namespace IRComponents

#endif /* COMPONENT_CYCLE_H */
