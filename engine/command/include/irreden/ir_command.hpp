#ifndef IR_COMMAND_H
#define IR_COMMAND_H

#include <irreden/ir_input.hpp>

#include <irreden/command/ir_command_types.hpp>
#include <irreden/command/command_manager.hpp>

#include <iterator>
#include <span>
#include <string>
#include <utility>

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

/// Reports whether the (@p inputType, @p triggerStatus, @p button) triple
/// already has a binding. Forwards to @ref CommandManager::isButtonBound —
/// see it for the scan target, the deliberate modifier-blindness, the MIDI
/// carve-out, and the cost contract.
///
/// The point of the query is to let creation code guard an ad-hoc bind
/// against what the engine already registered, instead of mirroring the
/// engine's key list in a hand-maintained "reserved keys" table that drifts
/// (`.claude/rules/cpp-ecs.md` §"System-owned invariants"). No dedup policy
/// is imposed: @ref createCommand still appends unconditionally, and stacking
/// one key under different modifier masks stays legal.
inline bool
isButtonBound(IRInput::InputTypes inputType, IRInput::ButtonStatuses triggerStatus, int button) {
    return getCommandManager().isButtonBound(inputType, triggerStatus, button);
}

/// Sentinel returned by @ref bindPrefabCommand on a name that has no
/// `Command<NAME>` specialization. `CommandId` is `uint32_t`, so the
/// max-value spelling lets callers distinguish a real registration
/// (always less than `m_userCommands.size()` at the time of call) from
/// the unimplemented-name path.
inline constexpr CommandId kInvalidCommandId = ~CommandId{0};

/// One row of the command catalog: the display label and the help-overlay
/// description for a `CommandNames` value. Descriptions are engine-public
/// text rendered by the uppercase-only trixel font — keep them to one short
/// clause (~40 chars) so a line fits the overlay column.
struct CommandInfo {
    CommandNames name_;
    const char *displayName_;
    const char *description_;
};

/// The command catalog — one row per `CommandNames` value, indexed by the
/// enum value itself. Replaces the former hand-listed `commandNameToString`
/// switch, whose `default:` arm silently rendered omitted values as
/// "UNKNOWN"; the static_asserts below turn that omission class into a
/// compile error instead. Values with no `Command<NAME>` specialization
/// still get a row — the label is what `fireByName`'s diagnostic prints.
inline constexpr CommandInfo kCommandInfo[] = {
    {NULL_COMMAND, "NULL", ""},
    {EXAMPLE, "EXAMPLE", ""},
    {ZOOM_IN, "ZOOM IN", "ZOOM THE CAMERA IN ONE STEP"},
    {ZOOM_OUT, "ZOOM OUT", "ZOOM THE CAMERA OUT ONE STEP"},
    {BACKGROUND_ZOOM_IN, "BG ZOOM IN", "ZOOM THE BACKGROUND CANVAS IN"},
    {BACKGROUND_ZOOM_OUT, "BG ZOOM OUT", "ZOOM THE BACKGROUND CANVAS OUT"},
    {CLOSE_WINDOW, "CLOSE WINDOW", "CLOSE THE WINDOW AND EXIT"},
    {MOVE_CAMERA_LEFT_START, "CAMERA LEFT", "PAN THE CAMERA LEFT WHILE HELD"},
    {MOVE_CAMERA_RIGHT_START, "CAMERA RIGHT", "PAN THE CAMERA RIGHT WHILE HELD"},
    {MOVE_CAMERA_UP_START, "CAMERA UP", "PAN THE CAMERA UP WHILE HELD"},
    {MOVE_CAMERA_DOWN_START, "CAMERA DOWN", "PAN THE CAMERA DOWN WHILE HELD"},
    {MOVE_CAMERA_LEFT_END, "CAMERA LEFT END", "STOP PANNING THE CAMERA LEFT"},
    {MOVE_CAMERA_RIGHT_END, "CAMERA RIGHT END", "STOP PANNING THE CAMERA RIGHT"},
    {MOVE_CAMERA_UP_END, "CAMERA UP END", "STOP PANNING THE CAMERA UP"},
    {MOVE_CAMERA_DOWN_END, "CAMERA DOWN END", "STOP PANNING THE CAMERA DOWN"},
    {SCREENSHOT, "SCREENSHOT", "SAVE A SCREENSHOT OF THE WINDOW"},
    {SCREENSHOT_CANVAS, "SCREENSHOT CANVAS", "SAVE THE CANVAS WITHOUT OVERLAYS"},
    {RECORD_START, "RECORD START", "START RECORDING VIDEO"},
    {RECORD_STOP, "RECORD STOP", "STOP RECORDING VIDEO"},
    {RECORD_TOGGLE, "RECORD TOGGLE", "START OR STOP RECORDING VIDEO"},
    {STOP_VELOCITY, "STOP VELOCITY", "ZERO THE TARGET ENTITY VELOCITY"},
    {RESHAPE_SPHERE, "RESHAPE SPHERE", "RESHAPE THE TARGET INTO A SPHERE"},
    {RESHAPE_RECTANGULAR_PRISM, "RESHAPE PRISM", "RESHAPE THE TARGET INTO A BOX"},
    {RANDOMIZE_VOXELS, "RANDOMIZE VOXELS", "RECOLOR EVERY UNLOCKED VOXEL SET"},
    {LOCK_VOXEL_SCALE, "LOCK VOXEL SCALE", "PIN THE VOXEL SCALE TO ITS VALUE"},
    {UNLOCK_VOXEL_SCALE, "UNLOCK VOXEL SCALE", "LET THE VOXEL SCALE TRACK ZOOM"},
    {SPAWN_PARTICLE_MOUSE_POSITION, "SPAWN PARTICLE", "SPAWN A PARTICLE AT THE CURSOR"},
    {SET_TRIXEL_COLOR, "SET TRIXEL", "SET THE TRIXEL UNDER THE CURSOR"},
    {TOGGLE_PERIODIC_IDLE_PAUSE, "TOGGLE PAUSE", "PAUSE OR RESUME PERIODIC IDLE MOTION"},
    {TOGGLE_GUI, "TOGGLE GUI", "SHOW OR HIDE THE LEGACY GUI FLAG"},
    {GUI_ZOOM_IN, "GUI ZOOM IN", "ENLARGE THE GUI CANVAS SCALE"},
    {GUI_ZOOM_OUT, "GUI ZOOM OUT", "SHRINK THE GUI CANVAS SCALE"},
    {TOGGLE_CULLING_FREEZE, "TOGGLE CULL FREEZE", "FREEZE THE CULL VIEWPORT AT THIS POSE"},
    {TOGGLE_CULLING_MINIMAP, "TOGGLE MINIMAP", "SHOW OR HIDE THE CULLING MINIMAP"},
    {TOGGLE_HELP_OVERLAY, "TOGGLE HELP", "SHOW OR HIDE THIS COMMAND LIST"},
    {TOGGLE_SETTINGS_MENU, "TOGGLE SETTINGS", "OPEN OR CLOSE THE SETTINGS MENU"},
};

static_assert(
    static_cast<int>(std::size(kCommandInfo)) == kCommandNameCount,
    "kCommandInfo needs one row per CommandNames value — add the row (and bump "
    "kCommandNameCount) in the same change that adds the enum entry"
);

/// Compile-time check that row `i` describes enum value `i`, so the O(1)
/// index lookup below is sound and a row inserted in the wrong place can't
/// silently mislabel two commands.
inline constexpr bool commandInfoRowsAligned() {
    for (int i = 0; i < kCommandNameCount; ++i) {
        if (static_cast<int>(kCommandInfo[i].name_) != i) {
            return false;
        }
    }
    return true;
}
static_assert(
    commandInfoRowsAligned(),
    "kCommandInfo rows must appear in CommandNames declaration order — the table is indexed "
    "by enum value"
);

/// Register a declarative @ref DefaultBinding manifest, honoring @p overrides.
///
/// The one registration primitive behind the engine's default-key suites and
/// any caller-authored binding table: filter by `overrides.omit`, substitute
/// buttons per `overrides.remap`, then dispatch each surviving row through
/// @ref bindPrefabCommand. Rows register in manifest order, so the debug help
/// overlay (which lists `PRESSED` registrations in registration order) reads
/// the same as the equivalent hand-written `createCommand<NAME>` sequence.
///
/// A row whose command has no `Command<NAME>` specialization logs an error via
/// `bindPrefabCommand` and asserts in debug — a manifest naming a command that
/// is missing from that switch must fail loudly at registration rather than
/// silently thinning the suite.
void registerBindings(
    std::span<const DefaultBinding> bindings, const BindingOverrides &overrides = {}
);

/// Returns a short human-readable label for @p name (e.g. "ZOOM IN").
/// Used by the help overlay and `buildCommandListText()`. Out-of-range
/// values (a cast from a bad Lua integer) return "UNKNOWN"; every declared
/// enum value has a real label by the static_asserts above.
inline std::string commandNameToString(CommandNames name) {
    const int index = static_cast<int>(name);
    if (index < 0 || index >= kCommandNameCount) {
        return "UNKNOWN";
    }
    return kCommandInfo[index].displayName_;
}

/// Returns the help-overlay description for @p name, or an empty string
/// for a value with no description (and for out-of-range values).
inline std::string commandDescription(CommandNames name) {
    const int index = static_cast<int>(name);
    if (index < 0 || index >= kCommandNameCount) {
        return "";
    }
    return kCommandInfo[index].description_;
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
/// @param name        Display label for the help overlay. Empty (the default)
///                    keeps the binding out of the registry entirely — the
///                    pre-existing behavior for every ad-hoc lambda binding.
/// @param description Short clause describing the effect, shown beside the
///                    label in the help overlay.
/// @return A `CommandId` handle for the binding.
template <typename Function>
CommandId createCommand(
    IRInput::InputTypes inputType,
    IRInput::ButtonStatuses triggerStatus,
    int button,
    Function command,
    IRInput::KeyModifierMask requiredModifiers = IRInput::kModifierNone,
    IRInput::KeyModifierMask blockedModifiers = IRInput::kModifierNone,
    std::string name = "",
    std::string description = ""
) {
    return getCommandManager().createCommand(
        inputType,
        triggerStatus,
        button,
        command,
        requiredModifiers,
        blockedModifiers,
        std::move(name),
        std::move(description)
    );
}

/// Registers a named engine command using its `Command<NAME>::create()` factory.
/// The enum value doubles as the identifier; the display name and description
/// both come from @ref kCommandInfo, so every prefab command appears in the
/// help overlay fully described with no per-creation wiring.
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
        commandNameToString(commandName),
        commandDescription(commandName)
    );
}

} // namespace IRCommand

#endif /* IR_COMMAND_H */
