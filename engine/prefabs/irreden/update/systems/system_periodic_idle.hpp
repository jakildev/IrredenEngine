#ifndef SYSTEM_PERIODIC_IDLE_H
#define SYSTEM_PERIODIC_IDLE_H

#include <irreden/ir_system.hpp>

#include <irreden/update/components/component_periodic_idle.hpp>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<PERIODIC_IDLE> {
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    void tick(C_PeriodicIdle &periodicIdle) {
        periodicIdle.tick();
    }

    static SystemId create() {
        return registerSystem<PERIODIC_IDLE, C_PeriodicIdle>("PeriodicIdle");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PERIODIC_IDLE_H */
