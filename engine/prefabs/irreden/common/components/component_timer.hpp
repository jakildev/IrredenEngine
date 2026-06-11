#ifndef COMPONENT_TIMER_H
#define COMPONENT_TIMER_H

// C_Timer — a generic sim-tick timer that fires when IRSim::tick() reaches
// `targetTick_`. One-shot when `intervalTicks_ == 0` (deactivates after firing);
// recurring when `intervalTicks_ > 0` (re-arms `targetTick_ += intervalTicks_`).
// Many can coexist; SYSTEM_TIMER_FIRE drives them.
//
// The fired event rides on the component (`fired_`), matching the
// events-as-components convention: a consumer system ordered AFTER TIMER_FIRE
// reads `fired_` the same tick, and the detector recomputes it every tick so it
// is self-clearing. A recurring timer that the sim overshoots by several
// intervals in one tick (large timeScale_) re-arms past every crossed interval
// and reports a single `fired_` for that tick.

#include <cstdint>
#include <string>
#include <utility>

namespace IRComponents {

struct C_Timer {
    std::string name_;
    std::uint64_t startTick_ = 0;      // sim tick the current countdown began (anchors timerFraction)
    std::uint64_t targetTick_ = 0;     // sim tick at which the timer fires
    std::uint64_t intervalTicks_ = 0;  // 0 = one-shot; > 0 = re-arm with this period
    bool active_ = true;

    bool fired_ = false;               // embedded event: true only on the firing tick

    C_Timer() = default;

    // startTick_ defaults to 0; IRSim::createTimer snapshots the live sim tick
    // into it so timerFraction is anchored correctly for timers armed after
    // sim tick 0. SYSTEM_TIMER_FIRE re-anchors it on each recurring re-arm.
    C_Timer(std::string name, std::uint64_t targetTick, std::uint64_t intervalTicks = 0)
        : name_{std::move(name)}
        , targetTick_{targetTick}
        , intervalTicks_{intervalTicks} {}
};

} // namespace IRComponents

#endif /* COMPONENT_TIMER_H */
