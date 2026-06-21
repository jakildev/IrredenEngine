#ifndef SYSTEM_CAMERA_MOUSE_ROTATE_H
#define SYSTEM_CAMERA_MOUSE_ROTATE_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_camera.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

// Ctrl + middle-mouse drag: orbit camera (yaw + pitch). Horizontal drag
// controls Z-yaw, vertical drag controls X-pitch.
//   - Ctrl + middle-drag         → Z-yaw about the EXACT screen center (the
//                                   CAMERA_CENTER default; no explicit focus).
//   - Ctrl + Shift + middle-drag → Z-yaw about the world point under the CURSOR
//                                   (pinned via IRRender::setRotationPivotFocus,
//                                   captured once at drag start; reverts to the
//                                   screen-center default when the drag ends).
// Middle drag without Ctrl falls through to CAMERA_MOUSE_PAN (unchanged).
template <> struct System<CAMERA_MOUSE_ROTATE> {
    static constexpr float kPixelsPerRevolution = 1280.0f;

    bool dragging_ = false;
    bool cursorPivot_ = false;
    vec2 dragStartMouse_ = vec2(0.0f);
    float dragStartYaw_ = 0.0f;
    float dragStartPitch_ = 0.0f;

    void tick(C_Camera &) {}

    void endTick() {
        const bool middlePressed =
            IRInput::checkKeyMouseButton(IRInput::kMouseButtonMiddle, IRInput::PRESSED);
        const bool middleDown =
            IRInput::checkKeyMouseButton(IRInput::kMouseButtonMiddle, IRInput::HELD);
        const bool ctrlHeld = IRInput::checkKeyMouseModifiers(IRInput::kModifierControl);
        const bool shiftHeld = IRInput::checkKeyMouseModifiers(IRInput::kModifierShift);

        if (middlePressed && ctrlHeld && !dragging_) {
            dragging_ = true;
            dragStartMouse_ = IRInput::getMousePositionScreen();
            dragStartYaw_ = IRPrefab::Camera::getYaw();
            dragStartPitch_ = IRPrefab::Camera::getPitch();
            // Cursor-pivot mode (Shift): pin the world point under the cursor —
            // captured once, in the current view — so yaw rotates about it.
            // Plain Ctrl-drag clears any focus so yaw rotates about screen center.
            cursorPivot_ = shiftHeld;
            if (cursorPivot_) {
                IRRender::setRotationPivotFocus(IRRender::mouseWorldPos3DAtIsoDepth(0.0f));
            } else {
                IRRender::clearRotationPivotFocus();
            }
        }

        if (dragging_ && middleDown) {
            const vec2 currentMouse = IRInput::getMousePositionScreen();
            const float deltaX = currentMouse.x - dragStartMouse_.x;
            const float deltaY = currentMouse.y - dragStartMouse_.y;
            const float yawDelta = (deltaX / kPixelsPerRevolution) * IRMath::kTwoPi;
            // negate: screen +Y is down; pitch up requires negative deltaY
            const float pitchDelta = -(deltaY / kPixelsPerRevolution) * IRMath::kTwoPi;
            IRPrefab::Camera::setYawPitch(dragStartYaw_ + yawDelta, dragStartPitch_ + pitchDelta);
        } else {
            if (dragging_ && cursorPivot_) {
                // Drag ended: revert to the screen-center default.
                IRRender::clearRotationPivotFocus();
                cursorPivot_ = false;
            }
            dragging_ = false;
        }
    }

    static SystemId create() {
        return registerSystem<CAMERA_MOUSE_ROTATE, C_Camera>("CameraMouseRotate");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CAMERA_MOUSE_ROTATE_H */
