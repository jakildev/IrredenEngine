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
//
// File drops (bindFileDrops, called by bindLuaCommands() and bindLuaInput()):
//
//   for _, drop in ipairs(IRInput.droppedFiles()) do
//       -- drop.paths: absolute paths, each one readable; drop.x, drop.y: the
//       -- cursor at the drop, in the window points IRInput reports elsewhere
//       for _, path in ipairs(drop.paths) do IRFile.readText(path) end
//   end
//
// `droppedFiles()` returns the drops the window received since the previous
// frame, in arrival order, and an empty table when there were none. It is a
// poll, not a queue: every call in a frame returns the same drops, and a drop
// nobody polled that frame is gone. Poll once per frame from INPUT or RENDER;
// an UPDATE tick can run zero or several times in one frame.
//
// A drop is the user's consent to read what they dropped, so polling grants
// every returned path to this script's confined readers the way IRDialog.open
// grants a pick: a dropped file by exactly that path, a dropped folder by
// everything beneath it, read-only, for the session. A path that no longer
// resolves is left out, and a drop left with no paths is left out with it.
//
// `IRInput.injectFileDrop(paths, x, y)` is the synthetic counterpart, bound
// only by bindLuaInput(): `paths` a dense array of at least one string, `x` /
// `y` finite window points. It queues one drop for the next frame's poll and
// leaves the cursor where it is. An injected drop reaches `droppedFiles()`
// only while synthetic input is active, where it is granted exactly as a real
// one is: a script handed the synthetic-input surface stands in for the user.
// Contract: docs/design/native-file-dialog.md "Dropped files".

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/ir_input.hpp>
#include <irreden/script/lua_script.hpp>

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace IRScript::detail {

inline IRInput::PointerGestureEvent pointerGestureFromLua(const sol::table &table) {
    const auto number = [&](const char *name) {
        const sol::object value = table[name];
        if (!value.valid())
            return 0.0;
        if (value.get_type() != sol::type::number || !IRMath::isFinite(value.as<double>()))
            throw std::invalid_argument(
                std::string("IRInput.injectPointerGesture: ") + name + " must be a finite number"
            );
        return value.as<double>();
    };
    const auto integer = [&](const char *name, int maximum) {
        const double value = number(name);
        if (value < 0.0 || value > maximum || value != static_cast<int>(value))
            throw std::invalid_argument(
                std::string("IRInput.injectPointerGesture: invalid ") + name
            );
        return static_cast<int>(value);
    };
    IRInput::PointerGestureEvent event;
    event.kind_ = static_cast<IRInput::PointerGestureKind>(
        integer("kind", static_cast<int>(IRInput::PointerGestureKind::MAGNIFY))
    );
    event.source_ = static_cast<IRInput::PointerGestureSource>(
        integer("source", static_cast<int>(IRInput::PointerGestureSource::WHEEL))
    );
    constexpr int phaseMask = static_cast<int>(IRInput::PointerGesturePhase::BEGAN) |
                              static_cast<int>(IRInput::PointerGesturePhase::CHANGED) |
                              static_cast<int>(IRInput::PointerGesturePhase::ENDED) |
                              static_cast<int>(IRInput::PointerGesturePhase::CANCELLED) |
                              static_cast<int>(IRInput::PointerGesturePhase::MAY_BEGIN) |
                              static_cast<int>(IRInput::PointerGesturePhase::STATIONARY);
    event.phase_ = static_cast<IRInput::PointerGesturePhase>(integer("phase", phaseMask));
    event.momentumPhase_ =
        static_cast<IRInput::PointerGesturePhase>(integer("momentumPhase", phaseMask));
    event.modifiers_ = static_cast<std::uint8_t>(integer("modifiers", IRInput::kModifierAll));
    event.scroll_ = {number("x"), number("y")};
    event.windowPoint_ = {number("windowX"), number("windowY")};
    event.magnification_ = number("magnification");
    const sol::object inverted = table["directionInverted"];
    if (inverted.valid()) {
        if (inverted.get_type() != sol::type::boolean)
            throw std::invalid_argument(
                "IRInput.injectPointerGesture: directionInverted must be boolean"
            );
        event.directionInverted_ = inverted.as<bool>();
    }
    return event;
}

inline IRInput::FileDropEvent
fileDropFromLua(const sol::object &paths, const sol::object &x, const sol::object &y) {
    if (paths.get_type() != sol::type::table) {
        throw std::invalid_argument("IRInput.injectFileDrop: paths must be an array of strings");
    }
    const sol::table table = paths.as<sol::table>();
    const std::size_t count = table.size();
    std::size_t keys = 0;
    for (const auto &entry : table) {
        static_cast<void>(entry);
        ++keys;
    }
    if (count == 0 || keys != count) {
        throw std::invalid_argument(
            "IRInput.injectFileDrop: paths must be a dense array of at least one string"
        );
    }
    IRInput::FileDropEvent event;
    event.paths_.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        const sol::object path = table.raw_get<sol::object>(i);
        if (path.get_type() != sol::type::string) {
            throw std::invalid_argument(
                "IRInput.injectFileDrop: paths[" + std::to_string(i) + "] must be a string"
            );
        }
        event.paths_.push_back(path.as<std::string>());
    }
    const auto coordinate = [](const sol::object &value, const char *name) {
        if (value.get_type() != sol::type::number || !IRMath::isFinite(value.as<double>())) {
            throw std::invalid_argument(
                std::string("IRInput.injectFileDrop: ") + name + " must be a finite number"
            );
        }
        return value.as<double>();
    };
    event.windowPoint_ = {coordinate(x, "x"), coordinate(y, "y")};
    return event;
}

// Grants one dropped path to the script's confined readers: a file by that
// path alone, a folder with everything beneath it. False when it resolves to
// neither.
inline bool grantDroppedPath(PathGrants &grants, const std::string &path) {
    const std::filesystem::path dropped{std::u8string{path.begin(), path.end()}};
    return grants.grantFile(dropped) || grants.grantDirectory(dropped);
}

// Expose `IRInput.droppedFiles()`. Reading a drop is what grants its paths, so
// a drop no script polls widens nothing. Idempotent.
inline void bindFileDrops(LuaScript &script) {
    sol::state &lua = script.lua();
    if (!lua["IRInput"].valid()) {
        lua["IRInput"] = lua.create_table();
    }
    if (lua["IRInput"]["droppedFiles"].valid()) {
        return;
    }
    // The script outlives this closure: its sol::state is the first member
    // LuaScript destroys.
    LuaScript *owner = &script;
    lua["IRInput"]["droppedFiles"] = [owner](sol::this_state state) {
        sol::state_view view{state};
        sol::table drops = view.create_table();
        int dropIndex = 1;
        for (const IRInput::FileDropEvent &event : IRInput::droppedFiles()) {
            sol::table paths = view.create_table();
            int pathIndex = 1;
            for (const std::string &path : event.paths_) {
                if (grantDroppedPath(owner->pathGrants(), path)) {
                    paths[pathIndex++] = path;
                }
            }
            if (pathIndex == 1) {
                continue;
            }
            drops[dropIndex++] = view.create_table_with(
                "paths",
                paths,
                "x",
                event.windowPoint_.x,
                "y",
                event.windowPoint_.y
            );
        }
        return drops;
    };
}

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
// injectMouseMove, injectScroll, injectFileDrop}` as Lua functions. These
// forward to the `IRInput::` free functions in
// `engine/input/include/irreden/ir_input.hpp`. The inject calls assert if
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

    lua["IRInput"]["injectFileDrop"] =
        [](const sol::object &paths, const sol::object &x, const sol::object &y) {
            IRInput::injectFileDrop(fileDropFromLua(paths, x, y));
        };

    sol::table kind = lua.create_table();
#define IR_BIND_GESTURE_KIND(name)                                                                 \
    kind[#name] = static_cast<lua_Integer>(IRInput::PointerGestureKind::name)
    IR_BIND_GESTURE_KIND(SCROLL);
    IR_BIND_GESTURE_KIND(MAGNIFY);
#undef IR_BIND_GESTURE_KIND
    lua["IRInput"]["PointerGestureKind"] = kind;
    sol::table source = lua.create_table();
#define IR_BIND_GESTURE_SOURCE(name)                                                               \
    source[#name] = static_cast<lua_Integer>(IRInput::PointerGestureSource::name)
    IR_BIND_GESTURE_SOURCE(UNKNOWN);
    IR_BIND_GESTURE_SOURCE(PRECISE_SCROLL);
    IR_BIND_GESTURE_SOURCE(WHEEL);
#undef IR_BIND_GESTURE_SOURCE
    lua["IRInput"]["PointerGestureSource"] = source;
    sol::table phase = lua.create_table();
#define IR_BIND_GESTURE_PHASE(name)                                                                \
    phase[#name] = static_cast<lua_Integer>(IRInput::PointerGesturePhase::name)
    IR_BIND_GESTURE_PHASE(NONE);
    IR_BIND_GESTURE_PHASE(BEGAN);
    IR_BIND_GESTURE_PHASE(CHANGED);
    IR_BIND_GESTURE_PHASE(ENDED);
    IR_BIND_GESTURE_PHASE(CANCELLED);
    IR_BIND_GESTURE_PHASE(MAY_BEGIN);
    IR_BIND_GESTURE_PHASE(STATIONARY);
#undef IR_BIND_GESTURE_PHASE
    lua["IRInput"]["PointerGesturePhase"] = phase;
    lua["IRInput"]["injectPointerGesture"] = [](const sol::table &event) {
        IRInput::injectPointerGesture(pointerGestureFromLua(event));
    };
    lua["IRInput"]["pointerGestures"] = [&lua]() {
        sol::table events = lua.create_table();
        int index = 1;
        for (const auto &event : IRInput::pointerGestures()) {
            events[index++] = lua.create_table_with(
                "kind",
                static_cast<lua_Integer>(event.kind_),
                "source",
                static_cast<lua_Integer>(event.source_),
                "phase",
                static_cast<lua_Integer>(event.phase_),
                "momentumPhase",
                static_cast<lua_Integer>(event.momentumPhase_),
                "x",
                event.scroll_.x,
                "y",
                event.scroll_.y,
                "magnification",
                event.magnification_,
                "windowX",
                event.windowPoint_.x,
                "windowY",
                event.windowPoint_.y,
                "modifiers",
                event.modifiers_,
                "directionInverted",
                event.directionInverted_
            );
        }
        return events;
    };
}

} // namespace IRScript::detail

#endif /* LUA_INPUT_BINDINGS_H */
