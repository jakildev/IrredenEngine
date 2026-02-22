#ifndef SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H
#define SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H

#include <irreden/ir_system.hpp>

#include <irreden/update/components/component_periodic_idle.hpp>
#include <irreden/common/components/component_position_offset_3d.hpp>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<PERIODIC_IDLE_POSITION_OFFSET> {
    static SystemId create() {
        return createSystem<C_PeriodicIdle, C_PositionOffset3D>(
            "PeriodicIdlePositionOffset",
            [](C_PeriodicIdle &idle, C_PositionOffset3D &offset) { offset.pos_ = idle.getValue(); }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H */
