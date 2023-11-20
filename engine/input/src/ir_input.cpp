#include <irreden/ir_input.hpp>
#include <irreden/ir_ecs.hpp>
#include <irreden/ir_profile.hpp>

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

    C_MousePosition getMousePositionUpdate() {
        return getInputManager().getMousePositionUpdate();
    }
    C_MousePosition getMousePositionRender() {
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

} // namespace IRInput