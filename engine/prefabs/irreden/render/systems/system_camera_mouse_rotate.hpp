#ifndef SYSTEM_CAMERA_MOUSE_ROTATE_H
#define SYSTEM_CAMERA_MOUSE_ROTATE_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_camera_yaw.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

// Ctrl + middle-mouse drag: rotates camera yaw. Horizontal pixel delta
// maps linearly to yaw; one full revolution = kPixelsPerRevolution pixels.
// Middle drag without Ctrl falls through to CAMERA_MOUSE_PAN (unchanged).
template <> struct System<CAMERA_MOUSE_ROTATE> {
    static constexpr float kPixelsPerRevolution = 1280.0f;

    bool dragging_ = false;
    vec2 dragStartMouse_ = vec2(0.0f);
    float dragStartYaw_ = 0.0f;

    void tick(C_CameraYaw &) {}

    void endTick() {
        const bool middlePressed = IRInput::checkKeyMouseButton(
            IRInput::kMouseButtonMiddle, IRInput::PRESSED
        );
        const bool middleDown = IRInput::checkKeyMouseButton(
            IRInput::kMouseButtonMiddle, IRInput::HELD
        );
        const bool ctrlHeld =
            IRInput::checkKeyMouseModifiers(IRInput::kModifierControl);

        if (middlePressed && ctrlHeld && !dragging_) {
            dragging_ = true;
            dragStartMouse_ = IRInput::getMousePositionScreen();
            dragStartYaw_ = IRPrefab::Camera::getYaw();
        }

        if (dragging_ && middleDown) {
            const vec2 currentMouse = IRInput::getMousePositionScreen();
            const float deltaPx = currentMouse.x - dragStartMouse_.x;
            const float yawDelta =
                (deltaPx / kPixelsPerRevolution) * IRMath::kTwoPi;
            IRPrefab::Camera::setYaw(dragStartYaw_ + yawDelta);
        } else {
            dragging_ = false;
        }
    }

    static SystemId create() {
        return registerSystem<CAMERA_MOUSE_ROTATE, C_CameraYaw>(
            "CameraMouseRotate"
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CAMERA_MOUSE_ROTATE_H */
