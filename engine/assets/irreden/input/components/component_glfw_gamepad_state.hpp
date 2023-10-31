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
        IRButtonStatuses buttons_[15];
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
                    if(buttons_[i] == IRButtonStatuses::kNotHeld || buttons_[i] == IRButtonStatuses::kReleased)
                        buttons_[i] = IRButtonStatuses::kPressed;
                    else
                        buttons_[i] = IRButtonStatuses::kHeld;
                }
                if(state.buttons[i] == GLFW_RELEASE) {
                    if(buttons_[i] == IRButtonStatuses::kHeld || buttons_[i] == IRButtonStatuses::kPressed)
                        buttons_[i] = IRButtonStatuses::kReleased;
                    else
                        buttons_[i] = IRButtonStatuses::kNotHeld;
                }
            }
            std::memcpy(axes_, state.axes, sizeof(axes_));
        }

        bool checkButtonPressed(IRGamepadButtons button) const {
            return buttons_[static_cast<int>(button)] == IRButtonStatuses::kPressed;
        }

        bool checkButtonDown(IRGamepadButtons button) const {
            return buttons_[static_cast<int>(button)] == IRButtonStatuses::kHeld ||
                buttons_[static_cast<int>(button)] == IRButtonStatuses::kPressed;
        }

        bool checkButtonReleased(IRGamepadButtons button) const {
            return buttons_[static_cast<int>(button)] == IRButtonStatuses::kReleased;
        }

        float getAxisValue(IRGamepadAxes axis) const {
            return axes_[static_cast<int>(axis)];
        }

    };

} // namespace IRComponents

#endif /* COMPONENT_GLFW_GAMEPAD_STATE_H */
