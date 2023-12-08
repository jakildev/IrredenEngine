    /*
 * Project: Irreden Engine
 * File: system_input_gamepad.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_INPUT_GAMEPAD_H
#define SYSTEM_INPUT_GAMEPAD_H

#include <irreden/ir_ecs.hpp>
#include <irreden/ir_input.hpp>

#include <irreden/input/components/component_glfw_joystick.hpp>
#include <irreden/input/components/component_glfw_gamepad_state.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {

    template<>
    struct System<INPUT_GAMEPAD> {
        static SystemId create() {
            return createSystem<C_GLFWJoystick, C_GLFWGamepadState>(
                "InputGamepad",
                [](
                    C_GLFWJoystick& joystick,
                    C_GLFWGamepadState& gamepadState
                )
                {
                    gamepadState.updateState(
                        IRInput::getWindow().getGamepadState(joystick.joystickId_)
                    );
                }
            );
        }

    };

} // namespace IRECS

#endif /* SYSTEM_INPUT_GAMEPAD_H */
