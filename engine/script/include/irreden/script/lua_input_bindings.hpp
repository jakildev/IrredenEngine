#ifndef LUA_INPUT_BINDINGS_H
#define LUA_INPUT_BINDINGS_H

// Lua bindings for the IRInput synthetic-input surface — expose
// IRInput.KeyMouseButtons, IRInput.ButtonStatuses, and the inject/query
// functions (beginSyntheticInput, isSyntheticInputActive, injectButton,
// injectMouseMove, injectScroll) so Lua behavior-smoke tests can drive
// headless input without GLFW. The public `LuaScript::bindLuaInput()` calls
// all three helpers below. Each guard checks for the bound-table key it owns
// so a second call is a no-op (matching the bindInputEnums / bindCommandFunctions
// pattern in lua_command_bindings.hpp).
//
// These tables are DISTINCT from the short-name tables in bindInputEnums():
//   IRInput.KeyMouseButtons  — full C++ enum names (kKeyButtonSpace, ...)
//   IRInput.ButtonStatuses   — plural form; same values as IRInput.ButtonStatus
// The short-name IRInput.Key / IRInput.ButtonStatus tables remain available to
// creation command-binding code after bindLuaCommands(); these additions extend
// the namespace without displacing the existing entries.

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/ir_input.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript::detail {

// Populate `IRInput.KeyMouseButtons` with the full C++ enum names from
// `IRInput::KeyMouseButtons` (kKeyButtonSpace, kMouseButtonLeft, etc.).
// These literal names match the C++ constants so Lua callers and C++ callers
// share the same surface without a name-mapping step.
inline void bindKeyMouseButtonEnum(LuaScript &script) {
    sol::state &lua = script.lua();
    if (!lua["IRInput"].valid()) {
        lua["IRInput"] = lua.create_table();
    }
    if (lua["IRInput"]["KeyMouseButtons"].valid()) {
        return;
    }
    sol::table t = lua.create_table();
#define IR_BIND_KEYBTN(name) t[#name] = static_cast<lua_Integer>(IRInput::name)
    IR_BIND_KEYBTN(kNullButton);
    // Control / whitespace / navigation
    IR_BIND_KEYBTN(kKeyButtonMinus);
    IR_BIND_KEYBTN(kKeyButtonEqual);
    IR_BIND_KEYBTN(kKeyButtonLeftControl);
    IR_BIND_KEYBTN(kKeyButtonRightControl);
    IR_BIND_KEYBTN(kKeyButtonEnter);
    IR_BIND_KEYBTN(kKeyButtonTab);
    IR_BIND_KEYBTN(kKeyButtonBackspace);
    IR_BIND_KEYBTN(kKeyButtonInsert);
    IR_BIND_KEYBTN(kKeyButtonDelete);
    IR_BIND_KEYBTN(kKeyButtonEscape);
    IR_BIND_KEYBTN(kKeyButtonUp);
    IR_BIND_KEYBTN(kKeyButtonDown);
    IR_BIND_KEYBTN(kKeyButtonLeft);
    IR_BIND_KEYBTN(kKeyButtonRight);
    IR_BIND_KEYBTN(kKeyButtonPageUp);
    IR_BIND_KEYBTN(kKeyButtonPageDown);
    IR_BIND_KEYBTN(kKeyButtonHome);
    IR_BIND_KEYBTN(kKeyButtonEnd);
    IR_BIND_KEYBTN(kKeyButtonCapsLock);
    IR_BIND_KEYBTN(kKeyButtonScrollLock);
    IR_BIND_KEYBTN(kKeyButtonNumLock);
    IR_BIND_KEYBTN(kKeyButtonPrintScreen);
    IR_BIND_KEYBTN(kKeyButtonPause);
    // Function keys
    IR_BIND_KEYBTN(kKeyButtonF1);
    IR_BIND_KEYBTN(kKeyButtonF2);
    IR_BIND_KEYBTN(kKeyButtonF3);
    IR_BIND_KEYBTN(kKeyButtonF4);
    IR_BIND_KEYBTN(kKeyButtonF5);
    IR_BIND_KEYBTN(kKeyButtonF6);
    IR_BIND_KEYBTN(kKeyButtonF7);
    IR_BIND_KEYBTN(kKeyButtonF8);
    IR_BIND_KEYBTN(kKeyButtonF9);
    IR_BIND_KEYBTN(kKeyButtonF10);
    IR_BIND_KEYBTN(kKeyButtonF11);
    IR_BIND_KEYBTN(kKeyButtonF12);
    IR_BIND_KEYBTN(kKeyButtonF13);
    IR_BIND_KEYBTN(kKeyButtonF14);
    IR_BIND_KEYBTN(kKeyButtonF15);
    IR_BIND_KEYBTN(kKeyButtonF16);
    IR_BIND_KEYBTN(kKeyButtonF17);
    IR_BIND_KEYBTN(kKeyButtonF18);
    IR_BIND_KEYBTN(kKeyButtonF19);
    IR_BIND_KEYBTN(kKeyButtonF20);
    IR_BIND_KEYBTN(kKeyButtonF21);
    IR_BIND_KEYBTN(kKeyButtonF22);
    IR_BIND_KEYBTN(kKeyButtonF23);
    IR_BIND_KEYBTN(kKeyButtonF24);
    IR_BIND_KEYBTN(kKeyButtonF25);
    // Keypad
    IR_BIND_KEYBTN(kKeyButtonKP0);
    IR_BIND_KEYBTN(kKeyButtonKP1);
    IR_BIND_KEYBTN(kKeyButtonKP2);
    IR_BIND_KEYBTN(kKeyButtonKP3);
    IR_BIND_KEYBTN(kKeyButtonKP4);
    IR_BIND_KEYBTN(kKeyButtonKP5);
    IR_BIND_KEYBTN(kKeyButtonKP6);
    IR_BIND_KEYBTN(kKeyButtonKP7);
    IR_BIND_KEYBTN(kKeyButtonKP8);
    IR_BIND_KEYBTN(kKeyButtonKP9);
    IR_BIND_KEYBTN(kKeyButtonKPDecimal);
    IR_BIND_KEYBTN(kKeyButtonKPDivide);
    IR_BIND_KEYBTN(kKeyButtonKPMultiply);
    IR_BIND_KEYBTN(kKeyButtonKPSubtract);
    IR_BIND_KEYBTN(kKeyButtonKPAdd);
    IR_BIND_KEYBTN(kKeyButtonKPEnter);
    IR_BIND_KEYBTN(kKeyButtonKPEqual);
    // Modifiers
    IR_BIND_KEYBTN(kKeyButtonLeftShift);
    IR_BIND_KEYBTN(kKeyButtonRightShift);
    IR_BIND_KEYBTN(kKeyButtonLeftAlt);
    IR_BIND_KEYBTN(kKeyButtonRightAlt);
    IR_BIND_KEYBTN(kKeyButtonLeftSuper);
    IR_BIND_KEYBTN(kKeyButtonRightSuper);
    IR_BIND_KEYBTN(kKeyButtonMenu);
    // Space and punctuation
    IR_BIND_KEYBTN(kKeyButtonSpace);
    IR_BIND_KEYBTN(kKeyButtonApostrophe);
    IR_BIND_KEYBTN(kKeyButtonComma);
    IR_BIND_KEYBTN(kKeyButtonPeriod);
    IR_BIND_KEYBTN(kKeyButtonSlash);
    IR_BIND_KEYBTN(kKeyButtonSemicolon);
    IR_BIND_KEYBTN(kKeyButtonBackslash);
    IR_BIND_KEYBTN(kKeyButtonLeftBracket);
    IR_BIND_KEYBTN(kKeyButtonRightBracket);
    IR_BIND_KEYBTN(kKeyButtonGraveAccent);
    IR_BIND_KEYBTN(kKeyButtonWorld1);
    IR_BIND_KEYBTN(kKeyButtonWorld2);
    // Letters
    IR_BIND_KEYBTN(kKeyButtonA);
    IR_BIND_KEYBTN(kKeyButtonB);
    IR_BIND_KEYBTN(kKeyButtonC);
    IR_BIND_KEYBTN(kKeyButtonD);
    IR_BIND_KEYBTN(kKeyButtonE);
    IR_BIND_KEYBTN(kKeyButtonF);
    IR_BIND_KEYBTN(kKeyButtonG);
    IR_BIND_KEYBTN(kKeyButtonH);
    IR_BIND_KEYBTN(kKeyButtonI);
    IR_BIND_KEYBTN(kKeyButtonJ);
    IR_BIND_KEYBTN(kKeyButtonK);
    IR_BIND_KEYBTN(kKeyButtonL);
    IR_BIND_KEYBTN(kKeyButtonM);
    IR_BIND_KEYBTN(kKeyButtonN);
    IR_BIND_KEYBTN(kKeyButtonO);
    IR_BIND_KEYBTN(kKeyButtonP);
    IR_BIND_KEYBTN(kKeyButtonQ);
    IR_BIND_KEYBTN(kKeyButtonR);
    IR_BIND_KEYBTN(kKeyButtonS);
    IR_BIND_KEYBTN(kKeyButtonT);
    IR_BIND_KEYBTN(kKeyButtonU);
    IR_BIND_KEYBTN(kKeyButtonV);
    IR_BIND_KEYBTN(kKeyButtonW);
    IR_BIND_KEYBTN(kKeyButtonX);
    IR_BIND_KEYBTN(kKeyButtonY);
    IR_BIND_KEYBTN(kKeyButtonZ);
    // Digits
    IR_BIND_KEYBTN(kKeyButton0);
    IR_BIND_KEYBTN(kKeyButton1);
    IR_BIND_KEYBTN(kKeyButton2);
    IR_BIND_KEYBTN(kKeyButton3);
    IR_BIND_KEYBTN(kKeyButton4);
    IR_BIND_KEYBTN(kKeyButton5);
    IR_BIND_KEYBTN(kKeyButton6);
    IR_BIND_KEYBTN(kKeyButton7);
    IR_BIND_KEYBTN(kKeyButton8);
    IR_BIND_KEYBTN(kKeyButton9);
    // Mouse buttons
    IR_BIND_KEYBTN(kMouseButtonNull);
    IR_BIND_KEYBTN(kMouseButtonLeft);
    IR_BIND_KEYBTN(kMouseButtonRight);
    IR_BIND_KEYBTN(kMouseButtonMiddle);
#undef IR_BIND_KEYBTN
    lua["IRInput"]["KeyMouseButtons"] = t;
}

// Populate `IRInput.ButtonStatuses` with the full C++ enum names from
// `IRInput::ButtonStatuses`. The plural name matches the C++ type; the
// singular `IRInput.ButtonStatus` table (from bindInputEnums in
// lua_command_bindings.hpp) remains available for command-trigger registration.
// Both tables carry identical integer ordinals.
inline void bindButtonStatusesEnum(LuaScript &script) {
    sol::state &lua = script.lua();
    if (!lua["IRInput"].valid()) {
        lua["IRInput"] = lua.create_table();
    }
    if (lua["IRInput"]["ButtonStatuses"].valid()) {
        return;
    }
    sol::table t = lua.create_table();
#define IR_BIND_BTN_STS(name) t[#name] = static_cast<lua_Integer>(IRInput::name)
    IR_BIND_BTN_STS(NOT_HELD);
    IR_BIND_BTN_STS(PRESSED);
    IR_BIND_BTN_STS(HELD);
    IR_BIND_BTN_STS(RELEASED);
    IR_BIND_BTN_STS(PRESSED_AND_RELEASED);
#undef IR_BIND_BTN_STS
    lua["IRInput"]["ButtonStatuses"] = t;
}

// Expose `IRInput.{beginSyntheticInput, isSyntheticInputActive, injectButton,
// injectMouseMove, injectScroll}` as Lua functions. These forward to the
// `IRInput::` free functions in `engine/input/include/irreden/ir_input.hpp`.
// `injectButton` / `injectMouseMove` / `injectScroll` assert if
// `beginSyntheticInput` was not called first (mirroring the C++ API contract).
// Idempotent — the guard checks `IRInput.beginSyntheticInput`.
inline void bindSyntheticInput(LuaScript &script) {
    sol::state &lua = script.lua();
    if (!lua["IRInput"].valid()) {
        lua["IRInput"] = lua.create_table();
    }
    if (lua["IRInput"]["beginSyntheticInput"].valid()) {
        return;
    }

    lua["IRInput"]["beginSyntheticInput"] = []() { IRInput::beginSyntheticInput(); };

    lua["IRInput"]["isSyntheticInputActive"] = []() -> bool {
        return IRInput::isSyntheticInputActive();
    };

    lua["IRInput"]["injectButton"] = [](lua_Integer button, lua_Integer status) {
        IRInput::injectButton(
            static_cast<IRInput::KeyMouseButtons>(button),
            static_cast<IRInput::ButtonStatuses>(status)
        );
    };

    lua["IRInput"]["injectMouseMove"] = [](lua_Integer x, lua_Integer y) {
        IRInput::injectMouseMove(IRMath::ivec2{static_cast<int>(x), static_cast<int>(y)});
    };

    lua["IRInput"]["injectScroll"] = [](double xOffset, double yOffset) {
        IRInput::injectScroll(xOffset, yOffset);
    };
}

} // namespace IRScript::detail

#endif /* LUA_INPUT_BINDINGS_H */
