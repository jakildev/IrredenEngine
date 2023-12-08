/*
 * Project: Irreden Engine
 * File: ir_input.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_input.hpp>
#include <irreden/ir_ecs.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>

namespace IRInput {

    InputManager* g_inputManager = nullptr;
    InputManager& getInputManager() {
        IR_ASSERT(
            g_inputManager != nullptr,
            "InputManager not initialized"
        );
        return *g_inputManager;
    }

    IRGLFWWindow* g_irglfwWindow = nullptr;
    IRGLFWWindow& getWindow() {
        IR_ASSERT(
            g_irglfwWindow != nullptr,
            "IRGLFWWindow not initialized"
        );
        return *g_irglfwWindow;
    }

    bool checkKeyMouseButton(
        KeyMouseButtons button,
        ButtonStatuses checkStatus
    )
    {
        return getInputManager().checkButton(
            button,
            checkStatus
        );
    }

    vec2 getMousePositionUpdate() {
        return getInputManager().getMousePositionUpdate();
    }
    vec2 getMousePositionRender() {
        return getInputManager().getMousePositionRender();
    }

    int getNumButtonPressesThisFrame(KeyMouseButtons button) {
        return getInputManager().getButtonPressesThisFrame(button);
    }
    int getNumButtonReleasesThisFrame(KeyMouseButtons button) {
        return getInputManager().getButtonReleasesThisFrame(button);
    }

    void closeWindow() {
        getWindow().setShouldClose();
    }

    void getWindowSize(IRMath::ivec2& size) {
        getWindow().getWindowSize(size.x, size.y);
    }

    void getCursorPosition(IRMath::dvec2& pos) {
        getWindow().getCursorPosition(pos.x, pos.y);
    }

} // namespace IRInput