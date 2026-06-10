#ifndef COMPONENT_CYCLE_H
#define COMPONENT_CYCLE_H

// C_Cycle — a generic tick-aligned recurring period. Many can coexist (one per
// "day", "season", "boss_phase", ...). SYSTEM_CYCLE_BOUNDARY_DETECT advances the
// resolver state each UPDATE tick and, on the tick a period boundary is crossed,
// raises the embedded boundary event (`boundaryCrossed_` + from/to cycle number
// and segment index).
//
// Boundary events ride on the component itself rather than a separate event
// component, matching the engine's events-as-components convention (cf.
// C_ContactEvent's entered_/exited_ flags): a consumer system ordered AFTER
// CYCLE_BOUNDARY_DETECT reads `boundaryCrossed_` the same tick it fires, and the
// detector recomputes it every tick so it is self-clearing (no separate clear
// system needed). For continuous time-driven values (sun angle, color temp),
// read IRSim::cycleFraction(name) instead of waiting for the discrete boundary.
//
// Multi-breakpoint support: call addBreakpoint(fraction) (fraction in [0,1)) to
// divide each period into segments. With breakpoints at {0.25, 0.5, 0.75} the
// period has four segments (0..3) and the boundary event fires on EVERY crossing,
// not just the period wrap. Empty breakpoints = single period-wrap boundary (the
// original behavior). `segmentIndex_` always holds the current segment and is
// updated every tick; `fromSegment_`/`toSegment_` are set alongside the other
// event fields and are only meaningful when `boundaryCrossed_` is true.
// IRSim::cycleSegment(name) is the name-keyed query form.
//
// `lastCycleNum_` defaults to the unprimed sentinel so a cycle created at any
// sim tick (not just tick 0) does not fire a spurious boundary on its first
// evaluation — the detector primes both the cycle number and segment index
// silently, then fires on real crossings.

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace IRComponents {

struct C_Cycle {
    static constexpr std::uint64_t kUnprimed = std::numeric_limits<std::uint64_t>::max();
    // Inline array — avoids a heap allocation in the archetype column.
    // Seven breakpoints give eight segments, enough for dawn/noon/dusk-style use cases.
    static constexpr std::uint8_t kMaxBreakpoints = 7;

    std::string name_;
    std::uint64_t periodTicks_ = 1;   // length of one cycle in sim ticks (>= 1)
    std::uint64_t phaseOffset_ = 0;   // shifts where the cycle boundary lands

    // Intra-period breakpoints: sorted tick offsets in [0, periodTicks_).
    // Empty = single period-wrap boundary only.
    std::uint64_t breakpoints_[kMaxBreakpoints] = {};
    std::uint8_t numBreakpoints_ = 0;

    // Resolver state (kUnprimed until first detect).
    std::uint64_t lastCycleNum_ = kUnprimed;
    std::uint8_t lastSegmentIndex_ = 0;

    // Embedded boundary event. `boundaryCrossed_` fires on EVERY segment crossing,
    // including the period wrap. `fromCycle_`/`toCycle_` and `fromSegment_`/`toSegment_`
    // are valid only on the tick `boundaryCrossed_` is true.
    // `segmentIndex_` is updated every tick and always reflects the current segment.
    bool boundaryCrossed_ = false;
    std::uint64_t fromCycle_ = 0;
    std::uint64_t toCycle_ = 0;
    std::uint8_t segmentIndex_ = 0;
    std::uint8_t fromSegment_ = 0;
    std::uint8_t toSegment_ = 0;

    C_Cycle() = default;

    C_Cycle(std::string name, std::uint64_t periodTicks, std::uint64_t phaseOffset = 0)
        : name_{std::move(name)}
        , periodTicks_{periodTicks}
        , phaseOffset_{phaseOffset} {}

    // Which segment [0, numBreakpoints_] contains withinTick.
    // Segment 0 = before the first breakpoint; segment i+1 starts at breakpoints_[i].
    // Returns 0 when numBreakpoints_ == 0 (the no-breakpoint fast path).
    std::uint8_t computeSegment(std::uint64_t withinTick) const {
        std::uint8_t seg = 0;
        for (std::uint8_t i = 0; i < numBreakpoints_; ++i) {
            if (withinTick >= breakpoints_[i]) {
                seg = static_cast<std::uint8_t>(i + 1);
            }
        }
        return seg;
    }

    // Add a breakpoint at a fraction of the period (fraction in [0, 1)).
    // The breakpoint is stored as an absolute tick offset within the period and
    // inserted in sorted order. Call before the cycle enters the pipeline — changing
    // breakpoints after priming leaves `lastSegmentIndex_` stale for one tick.
    // Silently no-ops when: fraction is outside [0,1), periodTicks_ == 0, or the
    // array is full (kMaxBreakpoints reached).
    void addBreakpoint(float fraction) {
        if (numBreakpoints_ >= kMaxBreakpoints || periodTicks_ == 0) {
            return;
        }
        if (!(fraction >= 0.0f && fraction < 1.0f)) {
            return;
        }
        const std::uint64_t offset =
            static_cast<std::uint64_t>(fraction * static_cast<float>(periodTicks_));
        std::uint8_t i = numBreakpoints_;
        while (i > 0 && breakpoints_[i - 1] > offset) {
            breakpoints_[i] = breakpoints_[i - 1];
            --i;
        }
        breakpoints_[i] = offset;
        ++numBreakpoints_;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_CYCLE_H */
