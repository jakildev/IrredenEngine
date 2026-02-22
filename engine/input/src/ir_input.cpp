#include <irreden/ir_input.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>

namespace IRInput {

InputManager *g_inputManager = nullptr;
InputManager &getInputManager() {
    IR_ASSERT(g_inputManager != nullptr, "InputManager not initialized");
    return *g_inputManager;
}

bool checkKeyMouseButton(KeyMouseButtons button, ButtonStatuses checkStatus) {
    return getInputManager().checkButton(button, checkStatus);
}

bool checkKeyMouseModifiers(KeyModifierMask requiredModifiers, KeyModifierMask blockedModifiers) {
    const bool shiftDown = getInputManager().checkButtonDown(kKeyButtonLeftShift) ||
                           getInputManager().checkButtonDown(kKeyButtonRightShift);
    const bool controlDown = getInputManager().checkButtonDown(kKeyButtonLeftControl) ||
                             getInputManager().checkButtonDown(kKeyButtonRightControl);
    const bool altDown = getInputManager().checkButtonDown(kKeyButtonLeftAlt) ||
                         getInputManager().checkButtonDown(kKeyButtonRightAlt);

    if ((requiredModifiers & kModifierShift) != 0 && !shiftDown) {
        return false;
    }
    if ((requiredModifiers & kModifierControl) != 0 && !controlDown) {
        return false;
    }
    if ((requiredModifiers & kModifierAlt) != 0 && !altDown) {
        return false;
    }

    if ((blockedModifiers & kModifierShift) != 0 && shiftDown) {
        return false;
    }
    if ((blockedModifiers & kModifierControl) != 0 && controlDown) {
        return false;
    }
    if ((blockedModifiers & kModifierAlt) != 0 && altDown) {
        return false;
    }
    return true;
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

bool hasAnyButtonPressedThisFrame() {
    return getInputManager().hasAnyButtonPressedThisFrame();
}

} // namespace IRInput