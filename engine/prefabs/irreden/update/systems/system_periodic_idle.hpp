#ifndef SYSTEM_PERIODIC_IDLE_H
#define SYSTEM_PERIODIC_IDLE_H

#include <irreden/ir_system.hpp>

#include <irreden/update/components/component_periodic_idle.hpp>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<PERIODIC_IDLE> {
    static SystemId create() {
        return createSystem<C_PeriodicIdle>(
            "PeriodicIdle",
            [](C_PeriodicIdle &periodicIdle) { periodicIdle.tick(); });
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PERIODIC_IDLE_H */
