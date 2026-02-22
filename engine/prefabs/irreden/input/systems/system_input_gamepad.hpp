#ifndef SYSTEM_INPUT_GAMEPAD_H
#define SYSTEM_INPUT_GAMEPAD_H

#include <irreden/ir_window.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/input/components/component_glfw_joystick.hpp>
#include <irreden/input/components/component_glfw_gamepad_state.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<INPUT_GAMEPAD> {
    static SystemId create() {
        return createSystem<C_GLFWJoystick, C_GLFWGamepadState>(
            "InputGamepad",
            [](C_GLFWJoystick &joystick, C_GLFWGamepadState &gamepadState) {
                gamepadState.updateState(
                    IRWindow::getWindow().getGamepadState(joystick.joystickId_)
                );
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_INPUT_GAMEPAD_H */
