/*
 * Project: Irreden Engine
 * File: system_input_key_mouse.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_INPUT_KEY_MOUSE_H
#define SYSTEM_INPUT_KEY_MOUSE_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_input.hpp>

#include <irreden/input/components/component_keyboard_key_status.hpp>
#include <irreden/input/components/component_key_mouse_button.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRInput;
using namespace IRSystem;

namespace IRECS {

    template <>
    struct System<INPUT_KEY_MOUSE> {
        static SystemId create() {
            return createSystem<C_KeyStatus, C_KeyMouseButton>(
                "InputKeyMouse",
                [](
                    C_KeyStatus& keyStatus,
                    const C_KeyMouseButton& mouseButton
                )
                {
                    int mouseButtonPressCount =
                        IRInput::getNumButtonPressesThisFrame(
                            mouseButton.button_
                        );
                    int mouseButtonReleaseCount =
                        IRInput::getNumButtonReleasesThisFrame(
                            mouseButton.button_
                        );
                    if(keyStatus.status_ == PRESSED) {
                        keyStatus.status_ = HELD;
                    }
                    else if(keyStatus.status_ == RELEASED) {
                        keyStatus.status_ = NOT_HELD;
                    }

                    if(mouseButtonPressCount > 0 && mouseButtonReleaseCount <= 0) {
                        keyStatus.status_ = PRESSED;
                    }
                    else if(mouseButtonPressCount <= 0 && mouseButtonReleaseCount > 0) {
                        keyStatus.status_ = RELEASED;
                    }
                    else if(mouseButtonPressCount > 0 && mouseButtonReleaseCount > 0) {
                        keyStatus.status_ = PRESSED_AND_RELEASED;
                    }
                    keyStatus.pressedThisFrameCount_ = mouseButtonPressCount;
                    keyStatus.releasedThisFrameCount_ = mouseButtonReleaseCount;
                }
            );
        }
    };

} // namespace IRECS

#endif /* SYSTEM_INPUT_KEY_MOUSE_H */
