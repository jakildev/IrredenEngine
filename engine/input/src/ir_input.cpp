#include <irreden/ir_input.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_platform.hpp>
#include <irreden/ir_window.hpp>

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
    return modifiersMatch(requiredModifiers, blockedModifiers, heldKeyModifiers());
}

KeyModifierMask heldKeyModifiers() {
    const bool shiftDown = getInputManager().checkButtonDown(kKeyButtonLeftShift) ||
                           getInputManager().checkButtonDown(kKeyButtonRightShift);
    const bool controlDown = getInputManager().checkButtonDown(kKeyButtonLeftControl) ||
                             getInputManager().checkButtonDown(kKeyButtonRightControl);
    const bool altDown = getInputManager().checkButtonDown(kKeyButtonLeftAlt) ||
                         getInputManager().checkButtonDown(kKeyButtonRightAlt);

    KeyModifierMask held = kModifierNone;
    held |= shiftDown ? kModifierShift : kModifierNone;
    held |= controlDown ? kModifierControl : kModifierNone;
    held |= altDown ? kModifierAlt : kModifierNone;
    return held;
}

vec2 getMousePosition() {
    return getInputManager().getMousePosition();
}

vec2 getMousePositionScreen() {
    vec2 mouse = getMousePosition();
    if constexpr (IRPlatform::kGfx.flipMouseY_) {
        ivec2 windowSize{};
        IRWindow::getWindowSize(windowSize);
        mouse.y = static_cast<float>(windowSize.y) - mouse.y;
    }
    return mouse;
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

bool checkGamepadButton(GamepadButtons button, ButtonStatuses buttonStatus, int irGamepadId) {
    return getInputManager().checkGamepadButton(button, buttonStatus, irGamepadId);
}

float getGamepadAxis(GamepadAxes axis, int irGamepadId) {
    return getInputManager().getGamepadAxis(axis, irGamepadId);
}

int scrollDelta() {
    return getInputManager().getScrollSteps();
}

vec2 scrollOffset() {
    return getInputManager().getScrollOffset();
}

void beginSyntheticInput() {
    getInputManager().beginSyntheticInput();
}

bool isSyntheticInputActive() {
    return getInputManager().isSyntheticInputActive();
}

void injectMouseMove(IRMath::ivec2 screenPx) {
    getInputManager().injectMouseMove(screenPx);
}

void injectButton(KeyMouseButtons button, ButtonStatuses status) {
    getInputManager().injectButton(button, status);
}

void injectScroll(double xOffset, double yOffset) {
    getInputManager().injectScroll(xOffset, yOffset);
}

} // namespace IRInput
