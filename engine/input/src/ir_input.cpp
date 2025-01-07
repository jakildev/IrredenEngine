/*
 * Project: Irreden Engine
 * File: ir_input.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_input.hpp>
#include <irreden/ir_entity.hpp>
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

} // namespace IRInput