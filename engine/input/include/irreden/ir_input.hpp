#ifndef IR_INPUT_H
#define IR_INPUT_H

#include <irreden/ir_math.hpp>

#include <irreden/input/ir_input_types.hpp>
#include <irreden/input/input_manager.hpp>

namespace IRInput {

extern InputManager *g_inputManager;
InputManager &getInputManager();

bool checkKeyMouseButton(KeyMouseButtons button, ButtonStatuses buttonStatus);
bool checkKeyMouseModifiers(KeyModifierMask requiredModifiers,
                            KeyModifierMask blockedModifiers = kModifierNone);

// Everything should just use render mouse position prob...
IRMath::vec2 getMousePositionUpdate();
IRMath::vec2 getMousePositionRender();

// Internal use for key mouse input system
int getNumButtonPressesThisFrame(KeyMouseButtons button);
int getNumButtonReleasesThisFrame(KeyMouseButtons button);
bool hasAnyButtonPressedThisFrame();
} // namespace IRInput

#endif /* IR_INPUT_H */
