/*
 * Project: Irreden Engine
 * File: ir_input.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_INPUT_H
#define IR_INPUT_H

#include <irreden/input/ir_input_types.hpp>
#include <irreden/input/ir_glfw_window.hpp>
#include <irreden/input/input_manager.hpp>

namespace IRInput {

    extern InputManager* g_inputManager;
    InputManager& getInputManager();

    bool checkKeyMouseButton(
        KeyMouseButtons button,
        ButtonStatuses buttonStatus
    );

    // Everything should just use render mouse position prob...
    IRComponents::C_MousePosition getMousePositionUpdate();
    IRComponents::C_MousePosition getMousePositionRender();

    // Internal use for key mouse input system
    int getNumButtonPressesThisFrame(KeyMouseButtons button);
    int getNumButtonReleasesThisFrame(KeyMouseButtons button);
}

#endif /* IR_INPUT_H */
