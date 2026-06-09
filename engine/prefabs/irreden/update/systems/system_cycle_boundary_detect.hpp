#ifndef SYSTEM_CYCLE_BOUNDARY_DETECT_H
#define SYSTEM_CYCLE_BOUNDARY_DETECT_H

// SYSTEM_CYCLE_BOUNDARY_DETECT — for every C_Cycle, recomputes the current cycle
// index from the sim tick and raises the embedded boundary event
// (boundaryCrossed_ + fromCycle_/toCycle_) on the tick a period boundary is
// crossed. Register AFTER SIM_CLOCK_ADVANCE so it reads the advanced tick.
//
// The sim tick is cached once per frame in beginTick (member-on-System<N> form,
// not function-local static — see engine/system/CLAUDE.md). boundaryCrossed_ is
// recomputed every tick, so it is self-clearing: a consumer system ordered after
// this one reads the flag the same frame, and it resets next frame with no
// separate clear system. A cycle is primed silently on its first evaluation
// (lastCycleNum_ == kUnprimed) so one created mid-sim fires no spurious boundary.

#include <irreden/common/components/component_cycle.hpp>
#include <irreden/common/sim_clock.hpp>
#include <irreden/ir_system.hpp>

#include <cstdint>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<CYCLE_BOUNDARY_DETECT> {
    std::uint64_t simTick_ = 0;

    void beginTick() {
        simTick_ = IRSim::tick();
    }

    void tick(C_Cycle &cycle) {
        if (cycle.periodTicks_ == 0) {
            cycle.boundaryCrossed_ = false;
            return;
        }
        const std::uint64_t currentCycleNum =
            (simTick_ + cycle.phaseOffset_) / cycle.periodTicks_;

        if (cycle.lastCycleNum_ == C_Cycle::kUnprimed) {
            cycle.lastCycleNum_ = currentCycleNum;
            cycle.boundaryCrossed_ = false;
            return;
        }

        if (currentCycleNum != cycle.lastCycleNum_) {
            cycle.boundaryCrossed_ = true;
            cycle.fromCycle_ = cycle.lastCycleNum_;
            cycle.toCycle_ = currentCycleNum;
            cycle.lastCycleNum_ = currentCycleNum;
        } else {
            cycle.boundaryCrossed_ = false;
        }
    }

    static SystemId create() {
        return registerSystem<CYCLE_BOUNDARY_DETECT, C_Cycle>("CycleBoundaryDetect");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CYCLE_BOUNDARY_DETECT_H */
