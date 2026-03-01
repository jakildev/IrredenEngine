#ifndef IR_COMMAND_H
#define IR_COMMAND_H

#include <irreden/ir_input.hpp>

#include <irreden/command/ir_command_types.hpp>
#include <irreden/command/command_manager.hpp>

#include <string>

namespace IRCommand {

extern CommandManager *g_commandManager;
CommandManager &getCommandManager();

inline std::string commandNameToString(CommandNames name) {
    switch (name) {
    case ZOOM_IN:                   return "ZOOM IN";
    case ZOOM_OUT:                  return "ZOOM OUT";
    case BACKGROUND_ZOOM_IN:        return "BG ZOOM IN";
    case BACKGROUND_ZOOM_OUT:       return "BG ZOOM OUT";
    case CLOSE_WINDOW:              return "CLOSE WINDOW";
    case MOVE_CAMERA_LEFT_START:    return "CAMERA LEFT";
    case MOVE_CAMERA_RIGHT_START:   return "CAMERA RIGHT";
    case MOVE_CAMERA_UP_START:      return "CAMERA UP";
    case MOVE_CAMERA_DOWN_START:    return "CAMERA DOWN";
    case MOVE_CAMERA_LEFT_END:      return "CAMERA LEFT END";
    case MOVE_CAMERA_RIGHT_END:     return "CAMERA RIGHT END";
    case MOVE_CAMERA_UP_END:        return "CAMERA UP END";
    case MOVE_CAMERA_DOWN_END:      return "CAMERA DOWN END";
    case SCREENSHOT:                return "SCREENSHOT";
    case RECORD_START:              return "RECORD START";
    case RECORD_STOP:               return "RECORD STOP";
    case RECORD_TOGGLE:             return "RECORD TOGGLE";
    case TOGGLE_GUI:                return "TOGGLE GUI";
    case GUI_ZOOM_IN:               return "GUI ZOOM IN";
    case GUI_ZOOM_OUT:              return "GUI ZOOM OUT";
    case TOGGLE_PERIODIC_IDLE_PAUSE:return "TOGGLE PAUSE";
    case SAVE_MAIN_CANVAS_TRIXELS:  return "SAVE CANVAS";
    case SET_TRIXEL_COLOR:          return "SET TRIXEL";
    case RANDOMIZE_VOXELS:          return "RANDOMIZE VOXELS";
    case SPAWN_PARTICLE_MOUSE_POSITION: return "SPAWN PARTICLE";
    default:                        return "UNKNOWN";
    }
}

inline std::string keyButtonToString(int button) {
    using namespace IRInput;
    switch (button) {
    case kKeyButtonA: return "A"; case kKeyButtonB: return "B";
    case kKeyButtonC: return "C"; case kKeyButtonD: return "D";
    case kKeyButtonE: return "E"; case kKeyButtonF: return "F";
    case kKeyButtonG: return "G"; case kKeyButtonH: return "H";
    case kKeyButtonI: return "I"; case kKeyButtonJ: return "J";
    case kKeyButtonK: return "K"; case kKeyButtonL: return "L";
    case kKeyButtonM: return "M"; case kKeyButtonN: return "N";
    case kKeyButtonO: return "O"; case kKeyButtonP: return "P";
    case kKeyButtonQ: return "Q"; case kKeyButtonR: return "R";
    case kKeyButtonS: return "S"; case kKeyButtonT: return "T";
    case kKeyButtonU: return "U"; case kKeyButtonV: return "V";
    case kKeyButtonW: return "W"; case kKeyButtonX: return "X";
    case kKeyButtonY: return "Y"; case kKeyButtonZ: return "Z";
    case kKeyButton0: return "0"; case kKeyButton1: return "1";
    case kKeyButton2: return "2"; case kKeyButton3: return "3";
    case kKeyButton4: return "4"; case kKeyButton5: return "5";
    case kKeyButton6: return "6"; case kKeyButton7: return "7";
    case kKeyButton8: return "8"; case kKeyButton9: return "9";
    case kKeyButtonF1: return "F1"; case kKeyButtonF2: return "F2";
    case kKeyButtonF3: return "F3"; case kKeyButtonF4: return "F4";
    case kKeyButtonF5: return "F5"; case kKeyButtonF6: return "F6";
    case kKeyButtonF7: return "F7"; case kKeyButtonF8: return "F8";
    case kKeyButtonF9: return "F9"; case kKeyButtonF10: return "F10";
    case kKeyButtonF11: return "F11"; case kKeyButtonF12: return "F12";
    case kKeyButtonSpace: return "SPACE";
    case kKeyButtonEnter: return "ENTER";
    case kKeyButtonTab: return "TAB";
    case kKeyButtonEscape: return "ESC";
    case kKeyButtonBackspace: return "BACKSPACE";
    case kKeyButtonMinus: return "-";
    case kKeyButtonEqual: return "=";
    case kKeyButtonGraveAccent: return "~";
    case kKeyButtonLeftShift: return "LSHIFT";
    case kKeyButtonRightShift: return "RSHIFT";
    case kKeyButtonLeftControl: return "LCTRL";
    case kKeyButtonRightControl: return "RCTRL";
    case kKeyButtonLeftAlt: return "LALT";
    case kKeyButtonRightAlt: return "RALT";
    case kKeyButtonUp: return "UP";
    case kKeyButtonDown: return "DOWN";
    case kKeyButtonLeft: return "LEFT";
    case kKeyButtonRight: return "RIGHT";
    case kKeyButtonComma: return ",";
    case kKeyButtonPeriod: return ".";
    case kKeyButtonSlash: return "/";
    case kKeyButtonSemicolon: return ";";
    case kKeyButtonApostrophe: return "'";
    case kKeyButtonLeftBracket: return "[";
    case kKeyButtonRightBracket: return "]";
    case kKeyButtonBackslash: return "\\";
    case kKeyButtonDelete: return "DEL";
    case kKeyButtonInsert: return "INS";
    case kKeyButtonHome: return "HOME";
    case kKeyButtonEnd: return "END";
    case kKeyButtonPageUp: return "PGUP";
    case kKeyButtonPageDown: return "PGDN";
    case kKeyButtonKPAdd: return "KP+";
    case kKeyButtonKPSubtract: return "KP-";
    case kKeyButtonKPMultiply: return "KP*";
    case kKeyButtonKPDivide: return "KP/";
    case kKeyButtonKPEnter: return "KPENTER";
    case kMouseButtonLeft: return "MOUSE L";
    case kMouseButtonRight: return "MOUSE R";
    case kMouseButtonMiddle: return "MOUSE M";
    default: return "?";
    }
}

inline std::string modifierString(IRInput::KeyModifierMask mods) {
    std::string result;
    if (mods & IRInput::kModifierShift) result += "SHIFT+";
    if (mods & IRInput::kModifierControl) result += "CTRL+";
    if (mods & IRInput::kModifierAlt) result += "ALT+";
    return result;
}

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

template <typename Function>
int createCommand(
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
