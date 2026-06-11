#ifndef SYSTEM_TIMER_FIRE_H
#define SYSTEM_TIMER_FIRE_H

// SYSTEM_TIMER_FIRE — for every C_Timer, raises the embedded fired_ event on the
// tick the sim reaches targetTick_. One-shot timers (intervalTicks_ == 0)
// deactivate after firing; recurring timers re-arm by advancing targetTick_ past
// every interval the sim crossed this tick (so a large timeScale_ overshoot
// reports a single fired_ rather than falling behind), and re-anchor startTick_
// to the current interval's start for timerFraction. Register AFTER
// SIM_CLOCK_ADVANCE so it reads the advanced tick.
//
// Sim tick cached once per frame in beginTick (member-on-System<N> form).
// fired_ is recomputed every tick (self-clearing); a consumer ordered after this
// system reads it the same frame.

#include <irreden/common/components/component_timer.hpp>
#include <irreden/common/sim_clock.hpp>
#include <irreden/ir_system.hpp>

#include <cstdint>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<TIMER_FIRE> {
    std::uint64_t simTick_ = 0;

    void beginTick() {
        simTick_ = IRSim::tick();
    }

    void tick(C_Timer &timer) {
        timer.fired_ = false;
        if (!timer.active_ || simTick_ < timer.targetTick_) {
            return;
        }
        timer.fired_ = true;
        if (timer.intervalTicks_ == 0) {
            timer.active_ = false;
            return;
        }
        do {
            timer.startTick_ = timer.targetTick_;
            timer.targetTick_ += timer.intervalTicks_;
        } while (simTick_ >= timer.targetTick_);
    }

    static SystemId create() {
        return registerSystem<TIMER_FIRE, C_Timer>("TimerFire");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_TIMER_FIRE_H */
