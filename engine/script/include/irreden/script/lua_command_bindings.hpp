#ifndef LUA_COMMAND_BINDINGS_H
#define LUA_COMMAND_BINDINGS_H

// Lua bindings for the IRCommand / IRInput surface — declare prefab + ad-hoc
// command bindings and fire them from Lua. The locked design lives at
// `docs/design/lua-input-commands.md`.
//
// Three idempotent bind helpers, each populating a slice of the Lua
// namespace. The public `LuaScript::bindLuaCommands()` calls all three.
// Each guard checks for the bound-table key it owns so a second call is a
// no-op (matching the `bindIRTimeEvents` / `bindSystemNameEnum` pattern).
//
// The `IR_BIND_*` macros mirror `IR_BIND_SYS` / `IR_BIND_TIME` in
// `lua_pipeline_bindings.hpp` — the C++ enum name doubles as the Lua key
// via stringization, so a typo on either side is a clear nil-access
// error rather than a silent miss.

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/ir_command.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript::detail {

// Populate `IRCommand.CommandName.X` as an integer table, hand-listed
// against `IRCommand::CommandNames` in
// `engine/command/include/irreden/command/ir_command_types.hpp`. Adding a
// new prefab command requires appending an `IR_BIND_CMD` line here in
// addition to the enum entry + `Command<NAME>::create()` specialization;
// missing it means `IRCommand.CommandName.NEW_NAME` resolves to nil in
// Lua and `IRCommand.bindPrefab` raises an "unknown command name" error.
inline void bindCommandNameEnum(LuaScript &script) {
    sol::state &lua = script.lua();
    if (!lua["IRCommand"].valid()) {
        lua["IRCommand"] = lua.create_table();
    }
    if (lua["IRCommand"]["CommandName"].valid()) {
        return;
    }
    sol::table t = lua.create_table();
#define IR_BIND_CMD(name) t[#name] = static_cast<lua_Integer>(IRCommand::name)
    IR_BIND_CMD(NULL_COMMAND);
    IR_BIND_CMD(EXAMPLE);
    IR_BIND_CMD(ZOOM_IN);
    IR_BIND_CMD(ZOOM_OUT);
    IR_BIND_CMD(BACKGROUND_ZOOM_IN);
    IR_BIND_CMD(BACKGROUND_ZOOM_OUT);
    IR_BIND_CMD(CLOSE_WINDOW);
    IR_BIND_CMD(MOVE_CAMERA_LEFT_START);
    IR_BIND_CMD(MOVE_CAMERA_RIGHT_START);
    IR_BIND_CMD(MOVE_CAMERA_UP_START);
    IR_BIND_CMD(MOVE_CAMERA_DOWN_START);
    IR_BIND_CMD(MOVE_CAMERA_LEFT_END);
    IR_BIND_CMD(MOVE_CAMERA_RIGHT_END);
    IR_BIND_CMD(MOVE_CAMERA_UP_END);
    IR_BIND_CMD(MOVE_CAMERA_DOWN_END);
    IR_BIND_CMD(SCREENSHOT);
    IR_BIND_CMD(SCREENSHOT_CANVAS);
    IR_BIND_CMD(RECORD_START);
    IR_BIND_CMD(RECORD_STOP);
    IR_BIND_CMD(RECORD_TOGGLE);
    IR_BIND_CMD(STOP_VELOCITY);
    IR_BIND_CMD(RESHAPE_SPHERE);
    IR_BIND_CMD(RESHAPE_RECTANGULAR_PRISM);
    IR_BIND_CMD(RANDOMIZE_VOXELS);
    IR_BIND_CMD(LOCK_VOXEL_SCALE);
    IR_BIND_CMD(UNLOCK_VOXEL_SCALE);
    IR_BIND_CMD(SPAWN_PARTICLE_MOUSE_POSITION);
    IR_BIND_CMD(SET_TRIXEL_COLOR);
    IR_BIND_CMD(TOGGLE_PERIODIC_IDLE_PAUSE);
    IR_BIND_CMD(TOGGLE_GUI);
    IR_BIND_CMD(GUI_ZOOM_IN);
    IR_BIND_CMD(GUI_ZOOM_OUT);
    IR_BIND_CMD(TOGGLE_CULLING_FREEZE);
    IR_BIND_CMD(TOGGLE_CULLING_MINIMAP);
#undef IR_BIND_CMD
    lua["IRCommand"]["CommandName"] = t;
}

// Populate `IRInput.{InputType, ButtonStatus, Key, Modifier,
// GamepadButton, GamepadAxis}` as integer tables, hand-listed against the
// enums in `engine/input/include/irreden/input/ir_input_types.hpp`. The
// modifier values are exposed as raw integers so a Lua caller can
// compose masks with LuaJIT's native `bit.bor` (see the design doc's Q6).
inline void bindInputEnums(LuaScript &script) {
    sol::state &lua = script.lua();
    if (!lua["IRInput"].valid()) {
        lua["IRInput"] = lua.create_table();
    }
    if (lua["IRInput"]["InputType"].valid()) {
        return;
    }

    sol::table inputType = lua.create_table();
#define IR_BIND_INPUT_TYPE(name) inputType[#name] = static_cast<lua_Integer>(IRInput::name)
    IR_BIND_INPUT_TYPE(KEY_MOUSE);
    IR_BIND_INPUT_TYPE(GAMEPAD);
    IR_BIND_INPUT_TYPE(MIDI_NOTE);
    IR_BIND_INPUT_TYPE(MIDI_CC);
#undef IR_BIND_INPUT_TYPE
    lua["IRInput"]["InputType"] = inputType;

    sol::table buttonStatus = lua.create_table();
#define IR_BIND_BTN_STATUS(name) buttonStatus[#name] = static_cast<lua_Integer>(IRInput::name)
    IR_BIND_BTN_STATUS(NOT_HELD);
    IR_BIND_BTN_STATUS(PRESSED);
    IR_BIND_BTN_STATUS(HELD);
    IR_BIND_BTN_STATUS(RELEASED);
    IR_BIND_BTN_STATUS(PRESSED_AND_RELEASED);
#undef IR_BIND_BTN_STATUS
    lua["IRInput"]["ButtonStatus"] = buttonStatus;

    sol::table modifier = lua.create_table();
    modifier["NONE"] = static_cast<lua_Integer>(IRInput::kModifierNone);
    modifier["SHIFT"] = static_cast<lua_Integer>(IRInput::kModifierShift);
    modifier["CONTROL"] = static_cast<lua_Integer>(IRInput::kModifierControl);
    modifier["ALT"] = static_cast<lua_Integer>(IRInput::kModifierAlt);
    lua["IRInput"]["Modifier"] = modifier;

    // Keyboard + mouse. Hand-listed against `KeyMouseButtons` in
    // `engine/input/include/irreden/input/ir_input_types.hpp`. The Lua
    // key drops the engine-internal `kKeyButton` / `kMouseButton`
    // prefixes so the surface reads as `IRInput.Key.A`,
    // `IRInput.Key.SPACE`, `IRInput.Key.MOUSE_LEFT`. The set is exhaustive
    // for the input enum's button half; adding a new key requires
    // editing both this table and the enum (same drift cost as
    // `commandNameToString`).
    sol::table key = lua.create_table();
#define IR_BIND_KEY(luaName, enumName) key[#luaName] = static_cast<lua_Integer>(IRInput::enumName)
    // Letters
    IR_BIND_KEY(A, kKeyButtonA);
    IR_BIND_KEY(B, kKeyButtonB);
    IR_BIND_KEY(C, kKeyButtonC);
    IR_BIND_KEY(D, kKeyButtonD);
    IR_BIND_KEY(E, kKeyButtonE);
    IR_BIND_KEY(F, kKeyButtonF);
    IR_BIND_KEY(G, kKeyButtonG);
    IR_BIND_KEY(H, kKeyButtonH);
    IR_BIND_KEY(I, kKeyButtonI);
    IR_BIND_KEY(J, kKeyButtonJ);
    IR_BIND_KEY(K, kKeyButtonK);
    IR_BIND_KEY(L, kKeyButtonL);
    IR_BIND_KEY(M, kKeyButtonM);
    IR_BIND_KEY(N, kKeyButtonN);
    IR_BIND_KEY(O, kKeyButtonO);
    IR_BIND_KEY(P, kKeyButtonP);
    IR_BIND_KEY(Q, kKeyButtonQ);
    IR_BIND_KEY(R, kKeyButtonR);
    IR_BIND_KEY(S, kKeyButtonS);
    IR_BIND_KEY(T, kKeyButtonT);
    IR_BIND_KEY(U, kKeyButtonU);
    IR_BIND_KEY(V, kKeyButtonV);
    IR_BIND_KEY(W, kKeyButtonW);
    IR_BIND_KEY(X, kKeyButtonX);
    IR_BIND_KEY(Y, kKeyButtonY);
    IR_BIND_KEY(Z, kKeyButtonZ);
    // Top-row digits
    IR_BIND_KEY(NUM_0, kKeyButton0);
    IR_BIND_KEY(NUM_1, kKeyButton1);
    IR_BIND_KEY(NUM_2, kKeyButton2);
    IR_BIND_KEY(NUM_3, kKeyButton3);
    IR_BIND_KEY(NUM_4, kKeyButton4);
    IR_BIND_KEY(NUM_5, kKeyButton5);
    IR_BIND_KEY(NUM_6, kKeyButton6);
    IR_BIND_KEY(NUM_7, kKeyButton7);
    IR_BIND_KEY(NUM_8, kKeyButton8);
    IR_BIND_KEY(NUM_9, kKeyButton9);
    // Function keys
    IR_BIND_KEY(F1, kKeyButtonF1);
    IR_BIND_KEY(F2, kKeyButtonF2);
    IR_BIND_KEY(F3, kKeyButtonF3);
    IR_BIND_KEY(F4, kKeyButtonF4);
    IR_BIND_KEY(F5, kKeyButtonF5);
    IR_BIND_KEY(F6, kKeyButtonF6);
    IR_BIND_KEY(F7, kKeyButtonF7);
    IR_BIND_KEY(F8, kKeyButtonF8);
    IR_BIND_KEY(F9, kKeyButtonF9);
    IR_BIND_KEY(F10, kKeyButtonF10);
    IR_BIND_KEY(F11, kKeyButtonF11);
    IR_BIND_KEY(F12, kKeyButtonF12);
    // Whitespace + control
    IR_BIND_KEY(SPACE, kKeyButtonSpace);
    IR_BIND_KEY(ENTER, kKeyButtonEnter);
    IR_BIND_KEY(TAB, kKeyButtonTab);
    IR_BIND_KEY(BACKSPACE, kKeyButtonBackspace);
    IR_BIND_KEY(ESCAPE, kKeyButtonEscape);
    IR_BIND_KEY(INSERT, kKeyButtonInsert);
    IR_BIND_KEY(DELETE, kKeyButtonDelete);
    IR_BIND_KEY(HOME, kKeyButtonHome);
    IR_BIND_KEY(END, kKeyButtonEnd);
    IR_BIND_KEY(PAGE_UP, kKeyButtonPageUp);
    IR_BIND_KEY(PAGE_DOWN, kKeyButtonPageDown);
    IR_BIND_KEY(CAPS_LOCK, kKeyButtonCapsLock);
    // Arrow keys
    IR_BIND_KEY(UP, kKeyButtonUp);
    IR_BIND_KEY(DOWN, kKeyButtonDown);
    IR_BIND_KEY(LEFT, kKeyButtonLeft);
    IR_BIND_KEY(RIGHT, kKeyButtonRight);
    // Modifiers (as keys, not as bitmask bits)
    IR_BIND_KEY(LEFT_SHIFT, kKeyButtonLeftShift);
    IR_BIND_KEY(RIGHT_SHIFT, kKeyButtonRightShift);
    IR_BIND_KEY(LEFT_CONTROL, kKeyButtonLeftControl);
    IR_BIND_KEY(RIGHT_CONTROL, kKeyButtonRightControl);
    IR_BIND_KEY(LEFT_ALT, kKeyButtonLeftAlt);
    IR_BIND_KEY(RIGHT_ALT, kKeyButtonRightAlt);
    // Punctuation
    IR_BIND_KEY(MINUS, kKeyButtonMinus);
    IR_BIND_KEY(EQUAL, kKeyButtonEqual);
    IR_BIND_KEY(COMMA, kKeyButtonComma);
    IR_BIND_KEY(PERIOD, kKeyButtonPeriod);
    IR_BIND_KEY(SLASH, kKeyButtonSlash);
    IR_BIND_KEY(SEMICOLON, kKeyButtonSemicolon);
    IR_BIND_KEY(APOSTROPHE, kKeyButtonApostrophe);
    IR_BIND_KEY(LEFT_BRACKET, kKeyButtonLeftBracket);
    IR_BIND_KEY(RIGHT_BRACKET, kKeyButtonRightBracket);
    IR_BIND_KEY(BACKSLASH, kKeyButtonBackslash);
    IR_BIND_KEY(GRAVE, kKeyButtonGraveAccent);
    // Mouse
    IR_BIND_KEY(MOUSE_LEFT, kMouseButtonLeft);
    IR_BIND_KEY(MOUSE_RIGHT, kMouseButtonRight);
    IR_BIND_KEY(MOUSE_MIDDLE, kMouseButtonMiddle);
#undef IR_BIND_KEY
    lua["IRInput"]["Key"] = key;

    sol::table gamepadButton = lua.create_table();
#define IR_BIND_GP_BTN(luaName, enumName)                                                          \
    gamepadButton[#luaName] = static_cast<lua_Integer>(IRInput::enumName)
    IR_BIND_GP_BTN(A, kGamepadButtonA);
    IR_BIND_GP_BTN(B, kGamepadButtonB);
    IR_BIND_GP_BTN(X, kGamepadButtonX);
    IR_BIND_GP_BTN(Y, kGamepadButtonY);
    IR_BIND_GP_BTN(LEFT_BUMPER, kGamepadButtonLeftBumper);
    IR_BIND_GP_BTN(RIGHT_BUMPER, kGamepadButtonRightBumper);
    IR_BIND_GP_BTN(BACK, kGamepadButtonBack);
    IR_BIND_GP_BTN(START, kGamepadButtonStart);
    IR_BIND_GP_BTN(GUIDE, kGamepadButtonGuide);
    IR_BIND_GP_BTN(LEFT_THUMB, kGamepadButtonLeftThumb);
    IR_BIND_GP_BTN(RIGHT_THUMB, kGamepadButtonRightThumb);
    IR_BIND_GP_BTN(D_PAD_UP, kGamepadButtonDPadUp);
    IR_BIND_GP_BTN(D_PAD_RIGHT, kGamepadButtonDPadRight);
    IR_BIND_GP_BTN(D_PAD_DOWN, kGamepadButtonDPadDown);
    IR_BIND_GP_BTN(D_PAD_LEFT, kGamepadButtonDPadLeft);
#undef IR_BIND_GP_BTN
    lua["IRInput"]["GamepadButton"] = gamepadButton;

    sol::table gamepadAxis = lua.create_table();
#define IR_BIND_GP_AXIS(luaName, enumName)                                                         \
    gamepadAxis[#luaName] = static_cast<lua_Integer>(IRInput::enumName)
    IR_BIND_GP_AXIS(LEFT_X, kGamepadAxisLeftX);
    IR_BIND_GP_AXIS(LEFT_Y, kGamepadAxisLeftY);
    IR_BIND_GP_AXIS(RIGHT_X, kGamepadAxisRightX);
    IR_BIND_GP_AXIS(RIGHT_Y, kGamepadAxisRightY);
    IR_BIND_GP_AXIS(LEFT_TRIGGER, kGamepadAxisLeftTrigger);
    IR_BIND_GP_AXIS(RIGHT_TRIGGER, kGamepadAxisRightTrigger);
#undef IR_BIND_GP_AXIS
    lua["IRInput"]["GamepadAxis"] = gamepadAxis;
}

// Expose `IRCommand.{bindPrefab, createCommand, fire, fireByName}` as
// Lua functions. `IRCommand.bindPrefab` forwards to
// `IRCommand::bindPrefabCommand` (one switch in ir_command.cpp).
// `IRCommand.createCommand` captures the Lua closure into a
// protected_function and registers a wrapper that traps errors
// in-VM so a Lua-body error doesn't propagate up the dispatch loop
// (lifetime contract: see design doc § "Lifetime contract").
inline void bindCommandFunctions(LuaScript &script) {
    sol::state &lua = script.lua();
    if (!lua["IRCommand"].valid()) {
        lua["IRCommand"] = lua.create_table();
    }
    if (lua["IRCommand"]["bindPrefab"].valid()) {
        return;
    }

    lua["IRCommand"]["bindPrefab"] = [](lua_Integer commandName,
                                        lua_Integer inputType,
                                        lua_Integer triggerStatus,
                                        lua_Integer button,
                                        sol::optional<lua_Integer> requiredMods,
                                        sol::optional<lua_Integer> blockedMods) -> lua_Integer {
        const auto required =
            static_cast<IRInput::KeyModifierMask>(requiredMods.value_or(IRInput::kModifierNone));
        const auto blocked =
            static_cast<IRInput::KeyModifierMask>(blockedMods.value_or(IRInput::kModifierNone));
        const IRCommand::CommandId id = IRCommand::bindPrefabCommand(
            static_cast<IRCommand::CommandNames>(commandName),
            static_cast<IRInput::InputTypes>(inputType),
            static_cast<IRInput::ButtonStatuses>(triggerStatus),
            static_cast<int>(button),
            required,
            blocked
        );
        return static_cast<lua_Integer>(id);
    };

    lua["IRCommand"]["createCommand"] = [](lua_Integer inputType,
                                           lua_Integer triggerStatus,
                                           lua_Integer button,
                                           sol::protected_function fn,
                                           sol::optional<lua_Integer> requiredMods,
                                           sol::optional<lua_Integer> blockedMods) -> lua_Integer {
        // Capture the protected_function by value into the lambda; the
        // CommandManager stores the wrapper as std::function<void()>, which
        // owns the protected_function for the manager's lifetime. World
        // teardown destroys CommandManager before sol::state (see design
        // doc § "Lifetime contract"), so by the time these wrappers are
        // destructed the protected_function still holds a live ref into
        // the open sol::state.
        auto body = [fn = std::move(fn)]() {
            sol::protected_function_result r = fn();
            if (!r.valid()) {
                sol::error e = r;
                IRE_LOG_ERROR("Lua command body error: {}", e.what());
            }
        };
        const auto required =
            static_cast<IRInput::KeyModifierMask>(requiredMods.value_or(IRInput::kModifierNone));
        const auto blocked =
            static_cast<IRInput::KeyModifierMask>(blockedMods.value_or(IRInput::kModifierNone));
        const IRCommand::CommandId id = IRCommand::getCommandManager().createCommand(
            static_cast<IRInput::InputTypes>(inputType),
            static_cast<IRInput::ButtonStatuses>(triggerStatus),
            static_cast<int>(button),
            std::move(body),
            required,
            blocked
        );
        return static_cast<lua_Integer>(id);
    };

    lua["IRCommand"]["fire"] = [](lua_Integer commandId) {
        IRCommand::fire(static_cast<IRCommand::CommandId>(commandId));
    };

    lua["IRCommand"]["fireByName"] = [](lua_Integer commandName) {
        IRCommand::fireByName(static_cast<IRCommand::CommandNames>(commandName));
    };
}

} // namespace IRScript::detail

#endif /* LUA_COMMAND_BINDINGS_H */
