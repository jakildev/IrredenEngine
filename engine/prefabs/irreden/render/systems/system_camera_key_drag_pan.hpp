#ifndef SYSTEM_CAMERA_KEY_DRAG_PAN_H
#define SYSTEM_CAMERA_KEY_DRAG_PAN_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_camera.hpp>
#include <irreden/common/components/component_position_2d_iso.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

// Space + left-mouse drag: pans the camera. Works on trackpad (no
// middle button) and mouse. Cursor delta maps through the same
// screenDeltaToIsoDelta projection as CAMERA_MOUSE_PAN.
//
// Modifier gate is latched at the left-press frame: if Space wasn't
// held at the moment left went PRESSED, no drag engages — keeps
// widget/gizmo/voxel-picking left-click interaction safe when the
// user is not in pan mode. While engaged, the drag tracks left-held
// regardless of whether Space stays down.
//
// Note: Space-down at left-press will engage camera pan AND fall
// through to whichever left-click consumers also fire (widget input,
// gizmo drag, voxel picking) — those systems do not check for Space.
// The convention matches Photoshop/Figma: hold Space FIRST as a
// mode-modal hand-tool gesture, in screen area clear of widgets and
// hovered gizmo handles.
template <> struct System<CAMERA_KEY_DRAG_PAN> {
    bool dragging_ = false;
    vec2 dragStartMouse_ = vec2(0.0f);
    vec2 dragStartCameraPos_ = vec2(0.0f);

    void tick(C_Camera &, C_Position2DIso &camPos) {
        const bool leftPressed = IRInput::checkKeyMouseButton(
            IRInput::kMouseButtonLeft, IRInput::PRESSED
        );
        const bool leftDown = IRInput::checkKeyMouseButton(
            IRInput::kMouseButtonLeft, IRInput::HELD
        ) || leftPressed;
        const bool spaceHeld = IRInput::checkKeyMouseButton(
            IRInput::kKeyButtonSpace, IRInput::HELD
        ) || IRInput::checkKeyMouseButton(
            IRInput::kKeyButtonSpace, IRInput::PRESSED
        );

        if (leftPressed && spaceHeld && !dragging_) {
            dragging_ = true;
            dragStartMouse_ = IRInput::getMousePositionScreen();
            dragStartCameraPos_ = camPos.pos_;
        }

        if (dragging_ && leftDown) {
            const vec2 currentMouse = IRInput::getMousePositionScreen();
            const vec2 deltaPx = currentMouse - dragStartMouse_;
            const vec2 deltaIso =
                screenDeltaToIsoDelta(deltaPx, IRRender::getTriangleStepSizeScreen());
            camPos.pos_ = dragStartCameraPos_ + deltaIso;
        } else {
            dragging_ = false;
        }
    }

    static SystemId create() {
        return registerSystem<CAMERA_KEY_DRAG_PAN, C_Camera, C_Position2DIso>(
            "CameraKeyDragPan"
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CAMERA_KEY_DRAG_PAN_H */
