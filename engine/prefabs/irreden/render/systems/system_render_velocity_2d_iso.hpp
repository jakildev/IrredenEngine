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

// Integrates the camera's WASD pan velocity into its iso-space position.
// The MOVE_CAMERA_* commands write a screen-aligned intent into
// C_Velocity2DIso (W = up-on-screen, A = left-on-screen, ...); that intent
// is only correct in iso space at yaw 0. Under camera Z-yaw the velocity
// must be remapped to the iso offset that moves the CAMERA_CENTER focus
// along screen axes — the same `cameraMoveRelativeToYaw` correction
// CAMERA_MOUSE_PAN / CAMERA_KEY_DRAG_PAN apply to their drag deltas, so
// keyboard and pointer panning stay consistent.
//
// The yaw is snapshotted once per frame in beginTick (serialized on the
// main thread regardless of PARALLEL_FOR) rather than read per-entity in
// the tick — the camera is the only entity carrying C_Velocity2DIso, and
// this keeps the parallel tick free of a getComponent. panYaw_ is 0 in
// ORIGIN pivot mode (yaw pivots about the world origin, so iso velocity
// needs no remap) and at yaw 0 `cameraMoveRelativeToYaw` is exactly the
// identity, so a static / cardinal scene stays byte-identical.
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
