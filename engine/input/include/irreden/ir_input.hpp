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

/// @name Internal input counters (used by input systems, not for general use)
/// @{
int getNumButtonPressesThisFrame(KeyMouseButtons button);
int getNumButtonReleasesThisFrame(KeyMouseButtons button);
bool hasAnyButtonPressedThisFrame();
/// @}
} // namespace IRInput

#endif /* IR_INPUT_H */
