#ifndef IR_INPUT_H
#define IR_INPUT_H

#include <irreden/ir_math.hpp>

#include <irreden/input/ir_input_types.hpp>
#include <irreden/input/input_manager.hpp>

namespace IRInput {

/// Global pointer to the active `InputManager`; managed by the engine runtime.
/// Prefer @ref getInputManager() for safe access.
extern InputManager *g_inputManager;
/// Returns a reference to the active `InputManager`. Asserts if not initialised.
InputManager &getInputManager();

/// Returns `true` if @p button is currently in @p buttonStatus this frame.
/// Queries the snapshot for the current pipeline event (INPUT / UPDATE / RENDER).
bool checkKeyMouseButton(KeyMouseButtons button, ButtonStatuses buttonStatus);
/// Returns `true` when all bits in @p requiredModifiers are set and none of the
/// bits in @p blockedModifiers are set in the current modifier state.
bool checkKeyMouseModifiers(
    KeyModifierMask requiredModifiers, KeyModifierMask blockedModifiers = kModifierNone
);

/// Cursor position in iso/world space for the current pipeline event's snapshot.
IRMath::vec2 getMousePosition();
/// Cursor position in screen (pixel) space for the current pipeline event's snapshot.
IRMath::vec2 getMousePositionScreen();

/// Returns `true` if gamepad @p button is currently in @p buttonStatus this frame.
/// Only gamepad 0 is queried by default; pass @p irGamepadId to select another.
/// Asserts if no gamepad was connected at engine startup.
bool checkGamepadButton(GamepadButtons button, ButtonStatuses buttonStatus, int irGamepadId = 0);

/// Current value of gamepad @p axis.
/// Sticks report [-1, 1]. Triggers report -1.0 when unpressed, +1.0 fully pressed.
/// Only gamepad 0 is queried by default; pass @p irGamepadId to select another.
/// Asserts if no gamepad was connected at engine startup.
float getGamepadAxis(GamepadAxes axis, int irGamepadId = 0);

/// @name Synthetic input (headless GUI/mouse verification harness, #1793)
/// Flip the active `InputManager` to consume injected events instead of GLFW
/// for the rest of the run, so a headless run can move the cursor and press
/// buttons. Inject calls assert if @ref beginSyntheticInput() was not called.
/// @{
/// Activate synthetic input for the remainder of the run (GLFW input suppressed).
void beginSyntheticInput();
/// Returns `true` when synthetic input is active.
bool isSyntheticInputActive();
/// Set the cursor (screen pixels) for the next frame's snapshot.
void injectMouseMove(IRMath::ivec2 screenPx);
/// Enqueue a button press/release, applied at the next frame boundary.
void injectButton(KeyMouseButtons button, ButtonStatuses status);
/// Enqueue a scroll delta, applied at the next frame boundary.
void injectScroll(double xOffset, double yOffset);
/// @}

/// @name Internal input counters (used by input systems, not for general use)
/// @{
int getNumButtonPressesThisFrame(KeyMouseButtons button);
int getNumButtonReleasesThisFrame(KeyMouseButtons button);
bool hasAnyButtonPressedThisFrame();
/// @}
} // namespace IRInput

#endif /* IR_INPUT_H */
