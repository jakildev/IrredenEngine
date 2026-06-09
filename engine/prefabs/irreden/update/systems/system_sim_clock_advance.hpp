#ifndef SYSTEM_SIM_CLOCK_ADVANCE_H
#define SYSTEM_SIM_CLOCK_ADVANCE_H

// SYSTEM_SIM_CLOCK_ADVANCE — advances the C_SimClock singleton once per UPDATE
// tick at the configured timeScale_. Register FIRST among the sim-clock systems
// so CYCLE_BOUNDARY_DETECT / TIMER_FIRE observe the new tick the same frame.
//
// timeScale_ == 1.0 takes a no-float fast path (++tickCount_). Otherwise the
// fractional rate accumulates in subTickAccum_ and only the whole-tick portion
// commits, so scale 0.5 advances every other tick and scale 2.0 advances two per
// tick — all with deterministic float ops (same inputs reproduce byte-identical
// state, satisfying the sim-clock determinism gate). timeScale_ <= 0 is paused.

#include <irreden/common/components/component_sim_clock.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <cstdint>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<SIM_CLOCK_ADVANCE> {
    void tick(C_SimClock &clock) {
        if (clock.timeScale_ == 1.0f) {
            ++clock.tickCount_;
            return;
        }
        if (clock.timeScale_ <= 0.0f) {
            return;
        }
        clock.subTickAccum_ += clock.timeScale_;
        const float whole = IRMath::floor(clock.subTickAccum_);
        clock.tickCount_ += static_cast<std::uint64_t>(whole);
        clock.subTickAccum_ -= whole;
    }

    static SystemId create() {
        return registerSystem<SIM_CLOCK_ADVANCE, C_SimClock>("SimClockAdvance");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SIM_CLOCK_ADVANCE_H */
