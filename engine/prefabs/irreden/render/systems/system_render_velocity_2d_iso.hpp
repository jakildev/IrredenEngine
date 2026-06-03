#ifndef SYSTEM_RENDER_VELOCITY_2D_ISO_H
#define SYSTEM_RENDER_VELOCITY_2D_ISO_H

#include <irreden/ir_time.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_camera.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/common/components/component_position_2d_iso.hpp>
#include <irreden/update/components/component_velocity_2d_iso.hpp>

using namespace IRComponents;

namespace IRSystem {

// Snapshot yaw in beginTick so the PARALLEL_FOR tick stays free of getComponent;
// panYaw_=0 in ORIGIN mode preserves raw-iso behaviour.
template <> struct System<RENDERING_VELOCITY_2D_ISO> {
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    float panYaw_ = 0.0f;

    void beginTick() {
        panYaw_ = IRRender::getRotationPivotMode() == IRRender::RotationPivotMode::CAMERA_CENTER
                      ? IRPrefab::Camera::getYaw()
                      : 0.0f;
    }

    void tick(C_Position2DIso &position, const C_Velocity2DIso &velocity) {
        position.pos_ += IRMath::cameraMoveRelativeToYaw(velocity.velocity_, panYaw_) *
                         // TODO: Delta time based on event registered in pipeline
                         vec2(IRTime::deltaTime(IRTime::RENDER));
    }

    static SystemId create() {
        return registerSystem<RENDERING_VELOCITY_2D_ISO, C_Position2DIso, C_Velocity2DIso>(
            "Camera"
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_RENDER_VELOCITY_2D_ISO_H */
