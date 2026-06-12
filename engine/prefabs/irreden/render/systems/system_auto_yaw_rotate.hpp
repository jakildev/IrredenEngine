#ifndef SYSTEM_AUTO_YAW_ROTATE_H
#define SYSTEM_AUTO_YAW_ROTATE_H

#include <irreden/ir_system.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_camera.hpp>

using namespace IRComponents;

namespace IRSystem {

// Rotates the camera's Z-yaw by a fixed angle each frame. Anchored on
// C_Camera so endTick fires exactly once per camera entity per frame.
template <> struct System<AUTO_YAW_ROTATE> {
    float radiansPerFrame_ = 0.0f;

    void tick(C_Camera &) {}
    void endTick() {
        IRPrefab::Camera::rotateYaw(radiansPerFrame_);
    }

    static SystemId create(float radiansPerFrame) {
        SystemId id = registerSystem<AUTO_YAW_ROTATE, C_Camera>("AutoYawRotate");
        getSystemParams<System<AUTO_YAW_ROTATE>>(id)->radiansPerFrame_ = radiansPerFrame;
        return id;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_AUTO_YAW_ROTATE_H */
