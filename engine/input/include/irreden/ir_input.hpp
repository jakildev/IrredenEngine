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

#include <irreden/ir_math.hpp>
#include <irreden/input/ir_input_types.hpp>
#include <irreden/input/ir_glfw_window.hpp>
#include <irreden/input/input_manager.hpp>

namespace IRInput {

    extern InputManager* g_inputManager;
    InputManager& getInputManager();
    extern IRGLFWWindow* g_irglfwWindow;
    IRGLFWWindow& getWindow();

    bool checkKeyMouseButton(
        KeyMouseButtons button,
        ButtonStatuses buttonStatus
    );

    // Everything should just use render mouse position prob...
    vec2 getMousePositionUpdate();
    vec2 getMousePositionRender();

    // Internal use for key mouse input system
    int getNumButtonPressesThisFrame(KeyMouseButtons button);
    int getNumButtonReleasesThisFrame(KeyMouseButtons button);

    void closeWindow();
    void getWindowSize(IRMath::ivec2& size);
    void getCursorPosition(IRMath::dvec2& pos);
}

#endif /* IR_INPUT_H */
