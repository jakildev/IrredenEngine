#ifndef SYSTEM_CAMERA_MOUSE_PAN_H
#define SYSTEM_CAMERA_MOUSE_PAN_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_camera.hpp>
#include <irreden/common/components/component_position_2d_iso.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<CAMERA_MOUSE_PAN> {
    bool dragging_ = false;
    vec2 dragStartMouse_ = vec2(0.0f);
    vec2 dragStartCameraPos_ = vec2(0.0f);

    void tick(C_Camera &, C_Position2DIso &camPos) {
        const bool middlePressed =
            IRInput::checkKeyMouseButton(IRInput::kMouseButtonMiddle, IRInput::PRESSED);
        const bool middleDown =
            IRInput::checkKeyMouseButton(IRInput::kMouseButtonMiddle, IRInput::HELD);

        if (middlePressed && !dragging_) {
            // Yield to CAMERA_MOUSE_ROTATE when Ctrl is held.
            if (!IRInput::checkKeyMouseModifiers(IRInput::kModifierControl)) {
                dragging_ = true;
                dragStartMouse_ = IRInput::getMousePositionScreen();
                dragStartCameraPos_ = camPos.pos_;
            }
        }

        if (dragging_ && middleDown) {
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
        return registerSystem<CAMERA_MOUSE_PAN, C_Camera, C_Position2DIso>("CameraMousePan");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CAMERA_MOUSE_PAN_H */
