#ifndef SYSTEM_RENDER_VELOCITY_2D_ISO_H
#define SYSTEM_RENDER_VELOCITY_2D_ISO_H

#include <irreden/ir_time.hpp>

#include <irreden/render/components/component_camera.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/common/components/component_position_2d_iso.hpp>
#include <irreden/update/components/component_velocity_2d_iso.hpp>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<RENDERING_VELOCITY_2D_ISO> {
    static SystemId create() {
        return createSystem<C_Position2DIso, C_Velocity2DIso>(
            "Camera", [](C_Position2DIso &position, const C_Velocity2DIso &velocity) {
                position.pos_ += velocity.velocity_ *
                                 // TODO: Delta time based on event registered in pipeline
                                 vec2(IRTime::deltaTime(IRTime::RENDER));
            });
    }
};

} // namespace IRSystem

#endif /* SYSTEM_RENDER_VELOCITY_2D_ISO_H */
