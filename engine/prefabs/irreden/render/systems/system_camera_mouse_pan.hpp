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
    static SystemId create() {
        static bool s_dragging = false;
        static vec2 s_dragStartMouse = vec2(0.0f);
        static vec2 s_dragStartCameraPos = vec2(0.0f);

        return createSystem<C_Camera, C_Position2DIso>(
            "CameraMousePan",
            [](C_Camera &, C_Position2DIso &camPos) {
                bool middlePressed = IRInput::checkKeyMouseButton(
                    IRInput::kMouseButtonMiddle,
                    IRInput::PRESSED
                );
                bool middleDown = IRInput::checkKeyMouseButton(
                    IRInput::kMouseButtonMiddle,
                    IRInput::HELD
                );

                if (middlePressed && !s_dragging) {
                    s_dragging = true;
                    s_dragStartMouse = IRInput::getMousePositionScreen();
                    s_dragStartCameraPos = camPos.pos_;
                }

                if (s_dragging && middleDown) {
                    vec2 currentMouse = IRInput::getMousePositionScreen();
                    vec2 deltaPx = currentMouse - s_dragStartMouse;
                    vec2 deltaIso =
                        screenDeltaToIsoDelta(deltaPx, IRRender::getTriangleStepSizeScreen());

                    camPos.pos_ = s_dragStartCameraPos + deltaIso;
                } else {
                    s_dragging = false;
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CAMERA_MOUSE_PAN_H */
