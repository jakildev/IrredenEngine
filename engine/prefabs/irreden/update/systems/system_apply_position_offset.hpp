#ifndef SYSTEM_APPLY_POSITION_OFFSET_H
#define SYSTEM_APPLY_POSITION_OFFSET_H

#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_position_offset_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<APPLY_POSITION_OFFSET> {
    static SystemId create() {
        return createSystem<C_PositionOffset3D, C_PositionGlobal3D>(
            "ApplyPositionOffset",
            [](C_PositionOffset3D &offset, C_PositionGlobal3D &globalPos) {
                globalPos.pos_ += offset.pos_;
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_APPLY_POSITION_OFFSET_H */
