/*
 * Project: Irreden Engine
 * File: component_glfw_gamepad_state.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_GLFW_GAMEPAD_STATE_H
#define COMPONENT_GLFW_GAMEPAD_STATE_H

#include <GLFW/glfw3.h>
#include <irreden/ir_input.hpp>
#include <cstring>

using namespace IRInput;

namespace IRComponents {

    struct C_GLFWGamepadState {
        // GLFWgamepadstate state_;
        ButtonStatuses buttons_[15];
        float axes_[6];

        C_GLFWGamepadState(const GLFWgamepadstate& state)
        {
            std::memcpy(buttons_, state.buttons, sizeof(buttons_));
            std::memcpy(axes_, state.axes, sizeof(axes_));
        }

        C_GLFWGamepadState()
        : buttons_{}
        , axes_{}
        {

        }

        void updateState(const GLFWgamepadstate& state) {
            for(int i=0; i < sizeof(state.buttons); i++) {
                if(state.buttons[i] == GLFW_PRESS) {
                    if(buttons_[i] == ButtonStatuses::NOT_HELD || buttons_[i] == ButtonStatuses::RELEASED)
                        buttons_[i] = ButtonStatuses::PRESSED;
                    else
                        buttons_[i] = ButtonStatuses::HELD;
                }
                if(state.buttons[i] == GLFW_RELEASE) {
                    if(buttons_[i] == ButtonStatuses::HELD || buttons_[i] == ButtonStatuses::PRESSED)
                        buttons_[i] = ButtonStatuses::RELEASED;
                    else
                        buttons_[i] = ButtonStatuses::NOT_HELD;
                }
            }
            std::memcpy(axes_, state.axes, sizeof(axes_));
        }

        bool checkButtonPressed(GamepadButtons button) const {
            return buttons_[static_cast<int>(button)] == ButtonStatuses::PRESSED;
        }

        bool checkButtonDown(GamepadButtons button) const {
            return buttons_[static_cast<int>(button)] == ButtonStatuses::HELD ||
                buttons_[static_cast<int>(button)] == ButtonStatuses::PRESSED;
        }

        bool checkButtonReleased(GamepadButtons button) const {
            return buttons_[static_cast<int>(button)] == ButtonStatuses::RELEASED;
        }

        float getAxisValue(GamepadAxes axis) const {
            return axes_[static_cast<int>(axis)];
        }

    };

} // namespace IRComponents

#endif /* COMPONENT_GLFW_GAMEPAD_STATE_H */
