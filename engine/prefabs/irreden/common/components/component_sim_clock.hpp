#ifndef COMPONENT_SIM_CLOCK_H
#define COMPONENT_SIM_CLOCK_H

// C_SimClock — the one canonical sim-time state for a world. Held as an ECS
// singleton (IREntity::singleton<C_SimClock>()); exactly one record exists per
// world. SYSTEM_SIM_CLOCK_ADVANCE advances `tickCount_` once per UPDATE tick at
// the configured `timeScale_`, and the IRSim:: service
// (engine/prefabs/irreden/common/sim_clock.hpp) reads/writes it.
//
// This is the "sim tick" half of the two-clock model: it pauses (timeScale_==0)
// and scales, so gameplay timers/cycles built on it freeze when the game is
// paused. The "engine tick" half (IRTime::tick(), always advancing) lives in
// engine/time and drives profilers / wall-clock-aligned infrastructure.
//
// `subTickAccum_` carries the sub-integer remainder when `timeScale_` is not a
// whole number (e.g. 0.5 advances `tickCount_` every other UPDATE tick). It is
// part of the deterministic state and round-trips with the clock.

#include <cstdint>

namespace IRComponents {

struct C_SimClock {
    std::uint64_t tickCount_ = 0;     // sim ticks (NOT engine ticks)
    float timeScale_ = 1.0f;          // 0 = paused, 1 = normal, N = fast, 1/N = slow
    float subTickAccum_ = 0.0f;       // sub-integer remainder for non-whole timeScale_

    C_SimClock() = default;
};

} // namespace IRComponents

#endif /* COMPONENT_SIM_CLOCK_H */
