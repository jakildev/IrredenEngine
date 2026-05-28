#ifndef SYSTEM_CAMERA_KEY_DRAG_ROTATE_H
#define SYSTEM_CAMERA_KEY_DRAG_ROTATE_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_camera.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

// Alt + left-mouse drag: orbit camera (yaw + pitch). Horizontal drag
// controls Z-yaw, vertical drag controls X-pitch. Mirror of
// CAMERA_MOUSE_ROTATE but bound to Alt+left so trackpad users (no
// middle button) have an orbit gesture.
//
// Modifier gate is latched at the left-press frame: if Alt wasn't
// held at PRESSED, no rotation engages — keeps widget/gizmo/picking
// left-click safe.
template <> struct System<CAMERA_KEY_DRAG_ROTATE> {
    static constexpr float kPixelsPerRevolution = 1280.0f;

    bool dragging_ = false;
    vec2 dragStartMouse_ = vec2(0.0f);
    float dragStartYaw_ = 0.0f;
    float dragStartPitch_ = 0.0f;

    void tick(C_Camera &) {}

    void endTick() {
        const bool leftPressed =
            IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::PRESSED);
        const bool leftDown =
            IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::HELD);
        const bool altHeld = IRInput::checkKeyMouseModifiers(IRInput::kModifierAlt);

        if (leftPressed && altHeld && !dragging_) {
            dragging_ = true;
            dragStartMouse_ = IRInput::getMousePositionScreen();
            dragStartYaw_ = IRPrefab::Camera::getYaw();
            dragStartPitch_ = IRPrefab::Camera::getPitch();
        }

        if (dragging_ && leftDown) {
            const vec2 currentMouse = IRInput::getMousePositionScreen();
            const float deltaX = currentMouse.x - dragStartMouse_.x;
            const float deltaY = currentMouse.y - dragStartMouse_.y;
            const float yawDelta = (deltaX / kPixelsPerRevolution) * IRMath::kTwoPi;
            // negate: screen +Y is down; pitch up requires negative deltaY
            const float pitchDelta = -(deltaY / kPixelsPerRevolution) * IRMath::kTwoPi;
            IRPrefab::Camera::setYawPitch(dragStartYaw_ + yawDelta, dragStartPitch_ + pitchDelta);
        } else {
            dragging_ = false;
        }
    }

    static SystemId create() {
        return registerSystem<CAMERA_KEY_DRAG_ROTATE, C_Camera>("CameraKeyDragRotate");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CAMERA_KEY_DRAG_ROTATE_H */
