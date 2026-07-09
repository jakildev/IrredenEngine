#ifndef IR_COMMAND_H
#define IR_COMMAND_H

#include <irreden/ir_input.hpp>

#include <irreden/command/ir_command_types.hpp>
#include <irreden/command/command_manager.hpp>

#include <string>

namespace IRCommand {

/// Global pointer to the active `CommandManager`; managed by the engine runtime.
/// Prefer @ref getCommandManager() for safe access.
extern CommandManager *g_commandManager;
/// Returns a reference to the active `CommandManager`. Asserts if not initialised.
CommandManager &getCommandManager();

/// Imperatively invoke a registered command by its runtime id. Forwards to
/// @ref CommandManager::fireUserCommand. Out-of-range ids are logged and
/// silently dropped — no exception, no crash. Use this to trigger a
/// command from a system body or any other non-input code path, e.g. a
/// scripted event firing a `SCREENSHOT` binding without a keystroke. The
/// id is the value returned by @ref createCommand (C++) or
/// `IRCommand.bindPrefab` / `IRCommand.createCommand` (Lua).
void fire(CommandId id);

/// Imperatively invoke a prefab command's `Command<NAME>::create()` body
/// without registering it against an input trigger. Hand-listed switch
/// over @ref CommandNames mirroring the shape of @ref commandNameToString,
/// but unlike `commandNameToString` (which returns "UNKNOWN" for omitted
/// values), `fireByName` logs an error for any enum value that has no
/// `Command<NAME>` specialization. Convenient for firing prefab commands
/// from Lua or from C++ code that wants the side effect without the
/// register-then-fire dance.
void fireByName(CommandNames name);

/// Register a prefab command's `Command<NAME>::create()` body against an
/// input trigger, dispatched at runtime by `name`. Same shape as the
/// existing `createCommand<NAME>(...)` template, but `name` is a runtime
/// enum value — useful when the binding caller (Lua, an editor UI) only
/// knows the command identity at runtime. Out-of-range or unimplemented
/// enum values log an error and return `kInvalidCommandId`. Counterpart
/// to @ref fireByName for the registration path.
CommandId bindPrefabCommand(
    CommandNames name,
    IRInput::InputTypes inputType,
    IRInput::ButtonStatuses triggerStatus,
    int button,
    IRInput::KeyModifierMask requiredModifiers = IRInput::kModifierNone,
    IRInput::KeyModifierMask blockedModifiers = IRInput::kModifierNone
);

/// Sentinel returned by @ref bindPrefabCommand on a name that has no
/// `Command<NAME>` specialization. `CommandId` is `uint32_t`, so the
/// max-value spelling lets callers distinguish a real registration
/// (always less than `m_userCommands.size()` at the time of call) from
/// the unimplemented-name path.
inline constexpr CommandId kInvalidCommandId = ~CommandId{0};

/// Returns a short human-readable label for @p name (e.g. "ZOOM IN").
/// Used by the debug overlay and `buildCommandListText()`.
inline std::string commandNameToString(CommandNames name) {
    switch (name) {
    case ZOOM_IN:
        return "ZOOM IN";
    case ZOOM_OUT:
        return "ZOOM OUT";
    case BACKGROUND_ZOOM_IN:
        return "BG ZOOM IN";
    case BACKGROUND_ZOOM_OUT:
        return "BG ZOOM OUT";
    case CLOSE_WINDOW:
        return "CLOSE WINDOW";
    case MOVE_CAMERA_LEFT_START:
        return "CAMERA LEFT";
    case MOVE_CAMERA_RIGHT_START:
        return "CAMERA RIGHT";
    case MOVE_CAMERA_UP_START:
        return "CAMERA UP";
    case MOVE_CAMERA_DOWN_START:
        return "CAMERA DOWN";
    case MOVE_CAMERA_LEFT_END:
        return "CAMERA LEFT END";
    case MOVE_CAMERA_RIGHT_END:
        return "CAMERA RIGHT END";
    case MOVE_CAMERA_UP_END:
        return "CAMERA UP END";
    case MOVE_CAMERA_DOWN_END:
        return "CAMERA DOWN END";
    case SCREENSHOT:
        return "SCREENSHOT";
    case RECORD_START:
        return "RECORD START";
    case RECORD_STOP:
        return "RECORD STOP";
    case RECORD_TOGGLE:
        return "RECORD TOGGLE";
    case TOGGLE_GUI:
        return "TOGGLE GUI";
    case GUI_ZOOM_IN:
        return "GUI ZOOM IN";
    case GUI_ZOOM_OUT:
        return "GUI ZOOM OUT";
    case TOGGLE_CULLING_MINIMAP:
        return "TOGGLE MINIMAP";
    case TOGGLE_PERIODIC_IDLE_PAUSE:
        return "TOGGLE PAUSE";
    case SET_TRIXEL_COLOR:
        return "SET TRIXEL";
    case RANDOMIZE_VOXELS:
        return "RANDOMIZE VOXELS";
    case SPAWN_PARTICLE_MOUSE_POSITION:
        return "SPAWN PARTICLE";
    default:
        return "UNKNOWN";
    }
}

/// Returns a short display name for a @ref IRInput::KeyMouseButtons value
/// (e.g. "A", "ENTER", "MOUSE L"). Used in the debug help overlay.
inline std::string keyButtonToString(int button) {
    using namespace IRInput;
    switch (button) {
    case kKeyButtonA:
        return "A";
    case kKeyButtonB:
        return "B";
    case kKeyButtonC:
        return "C";
    case kKeyButtonD:
        return "D";
    case kKeyButtonE:
        return "E";
    case kKeyButtonF:
        return "F";
    case kKeyButtonG:
        return "G";
    case kKeyButtonH:
        return "H";
    case kKeyButtonI:
        return "I";
    case kKeyButtonJ:
        return "J";
    case kKeyButtonK:
        return "K";
    case kKeyButtonL:
        return "L";
    case kKeyButtonM:
        return "M";
    case kKeyButtonN:
        return "N";
    case kKeyButtonO:
        return "O";
    case kKeyButtonP:
        return "P";
    case kKeyButtonQ:
        return "Q";
    case kKeyButtonR:
        return "R";
    case kKeyButtonS:
        return "S";
    case kKeyButtonT:
        return "T";
    case kKeyButtonU:
        return "U";
    case kKeyButtonV:
        return "V";
    case kKeyButtonW:
        return "W";
    case kKeyButtonX:
        return "X";
    case kKeyButtonY:
        return "Y";
    case kKeyButtonZ:
        return "Z";
    case kKeyButton0:
        return "0";
    case kKeyButton1:
        return "1";
    case kKeyButton2:
        return "2";
    case kKeyButton3:
        return "3";
    case kKeyButton4:
        return "4";
    case kKeyButton5:
        return "5";
    case kKeyButton6:
        return "6";
    case kKeyButton7:
        return "7";
    case kKeyButton8:
        return "8";
    case kKeyButton9:
        return "9";
    case kKeyButtonF1:
        return "F1";
    case kKeyButtonF2:
        return "F2";
    case kKeyButtonF3:
        return "F3";
    case kKeyButtonF4:
        return "F4";
    case kKeyButtonF5:
        return "F5";
    case kKeyButtonF6:
        return "F6";
    case kKeyButtonF7:
        return "F7";
    case kKeyButtonF8:
        return "F8";
    case kKeyButtonF9:
        return "F9";
    case kKeyButtonF10:
        return "F10";
    case kKeyButtonF11:
        return "F11";
    case kKeyButtonF12:
        return "F12";
    case kKeyButtonSpace:
        return "SPACE";
    case kKeyButtonEnter:
        return "ENTER";
    case kKeyButtonTab:
        return "TAB";
    case kKeyButtonEscape:
        return "ESC";
    case kKeyButtonBackspace:
        return "BACKSPACE";
    case kKeyButtonMinus:
        return "-";
    case kKeyButtonEqual:
        return "=";
    case kKeyButtonGraveAccent:
        return "~";
    case kKeyButtonLeftShift:
        return "LSHIFT";
    case kKeyButtonRightShift:
        return "RSHIFT";
    case kKeyButtonLeftControl:
        return "LCTRL";
    case kKeyButtonRightControl:
        return "RCTRL";
    case kKeyButtonLeftAlt:
        return "LALT";
    case kKeyButtonRightAlt:
        return "RALT";
    case kKeyButtonUp:
        return "UP";
    case kKeyButtonDown:
        return "DOWN";
    case kKeyButtonLeft:
        return "LEFT";
    case kKeyButtonRight:
        return "RIGHT";
    case kKeyButtonComma:
        return ",";
    case kKeyButtonPeriod:
        return ".";
    case kKeyButtonSlash:
        return "/";
    case kKeyButtonSemicolon:
        return ";";
    case kKeyButtonApostrophe:
        return "'";
    case kKeyButtonLeftBracket:
        return "[";
    case kKeyButtonRightBracket:
        return "]";
    case kKeyButtonBackslash:
        return "\\";
    case kKeyButtonDelete:
        return "DEL";
    case kKeyButtonInsert:
        return "INS";
    case kKeyButtonHome:
        return "HOME";
    case kKeyButtonEnd:
        return "END";
    case kKeyButtonPageUp:
        return "PGUP";
    case kKeyButtonPageDown:
        return "PGDN";
    case kKeyButtonKPAdd:
        return "KP+";
    case kKeyButtonKPSubtract:
        return "KP-";
    case kKeyButtonKPMultiply:
        return "KP*";
    case kKeyButtonKPDivide:
        return "KP/";
    case kKeyButtonKPEnter:
        return "KPENTER";
    case kMouseButtonLeft:
        return "MOUSE L";
    case kMouseButtonRight:
        return "MOUSE R";
    case kMouseButtonMiddle:
        return "MOUSE M";
    default:
        return "?";
    }
}

/// Formats an @ref IRInput::KeyModifierMask as a prefix string (e.g. "SHIFT+CTRL+").
/// Empty string if no modifiers are set.
inline std::string modifierString(IRInput::KeyModifierMask mods) {
    std::string result;
    if (mods & IRInput::kModifierShift)
        result += "SHIFT+";
    if (mods & IRInput::kModifierControl)
        result += "CTRL+";
    if (mods & IRInput::kModifierAlt)
        result += "ALT+";
    return result;
}

/// Builds a multi-line human-readable list of all registered `PRESSED` commands
/// with their modifier + key bindings. Used by the in-game debug help overlay.
/// Only `PRESSED`-status bindings appear; `HELD`/`RELEASED` bindings are excluded.
inline std::string buildCommandListText() {
    const auto &regs = getCommandManager().getCommandRegistrations();
    std::string text = "COMMANDS\n";
    for (const auto &reg : regs) {
        text += modifierString(reg.requiredModifiers);
        text += keyButtonToString(reg.button);
        text += ": ";
        text += reg.name;
        text += "\n";
    }
    return text;
}

/// Registers an ad-hoc command from a callable (lambda or functor).
/// @param inputType    Device class (KEY_MOUSE / GAMEPAD / MIDI_NOTE / MIDI_CC).
/// @param triggerStatus Button state that fires the command (PRESSED, HELD, …).
/// @param button       Raw button/key/note identifier.
/// @param command      The callable to invoke on trigger; `void()` signature.
/// @param requiredModifiers Modifier bits that must be active (KEY_MOUSE only).
/// @param blockedModifiers  Modifier bits that must be inactive (KEY_MOUSE only).
/// @return A `CommandId` handle for the binding.
template <typename Function>
CommandId createCommand(
    IRInput::InputTypes inputType,
    IRInput::ButtonStatuses triggerStatus,
    int button,
    Function command,
    IRInput::KeyModifierMask requiredModifiers = IRInput::kModifierNone,
    IRInput::KeyModifierMask blockedModifiers = IRInput::kModifierNone
) {
    return getCommandManager().createCommand(
        inputType,
        triggerStatus,
        button,
        command,
        requiredModifiers,
        blockedModifiers
    );
}

/// Registers a named engine command using its `Command<NAME>::create()` factory.
/// The enum value doubles as the identifier; the display name is derived from
/// @ref commandNameToString and appears in the debug help overlay.
template <CommandNames commandName>
CommandId createCommand(
    IRInput::InputTypes inputType,
    IRInput::ButtonStatuses triggerStatus,
    int button,
    IRInput::KeyModifierMask requiredModifiers = IRInput::kModifierNone,
    IRInput::KeyModifierMask blockedModifiers = IRInput::kModifierNone
) {
    return getCommandManager().createCommand(
        inputType,
        triggerStatus,
        button,
        Command<commandName>::create(),
        requiredModifiers,
        blockedModifiers,
        commandNameToString(commandName)
    );
}

} // namespace IRCommand

#endif /* IR_COMMAND_H */
