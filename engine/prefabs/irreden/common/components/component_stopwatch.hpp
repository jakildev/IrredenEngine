#ifndef COMPONENT_STOPWATCH_H
#define COMPONENT_STOPWATCH_H

// C_Stopwatch — a generic count-up timer measuring elapsed sim ticks since
// start/reset, with pause/resume. Many can coexist (HUD timers, "time in
// region", combo windows). Unlike C_Cycle / C_Timer there is no per-frame
// stopwatch system: elapsed is a pure function of the live sim tick, computed
// on read by IRSim::stopwatchElapsed(name), and pause/resume/reset are
// imperative IRSim:: calls that snapshot the sim tick. Keeping it
// computed-on-read avoids a no-op per-frame system over every stopwatch entity.
//
//   elapsed = pausedElapsed_ + (running_ ? simTick - startTick_ : 0)
//
// `startTick_` is the sim tick captured at the last start/resume/reset, so a
// stopwatch must be created through IRSim::createStopwatch (which snapshots the
// current sim tick) rather than constructed inline if it starts after sim tick 0.

#include <cstdint>
#include <string>
#include <utility>

namespace IRComponents {

struct C_Stopwatch {
    std::string name_;
    std::uint64_t startTick_ = 0;      // sim tick at last start/resume/reset
    std::uint64_t pausedElapsed_ = 0;  // elapsed accumulated before the current run segment
    bool running_ = true;

    C_Stopwatch() = default;

    explicit C_Stopwatch(std::string name)
        : name_{std::move(name)} {}

    C_Stopwatch(std::string name, std::uint64_t startTick)
        : name_{std::move(name)}
        , startTick_{startTick} {}
};

} // namespace IRComponents

#endif /* COMPONENT_STOPWATCH_H */
