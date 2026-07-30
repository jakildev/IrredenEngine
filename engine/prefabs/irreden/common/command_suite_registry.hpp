#ifndef COMMAND_SUITE_REGISTRY_H
#define COMMAND_SUITE_REGISTRY_H

// The engine's default keybindings, as data.
//
// Each engine-shipped suite has exactly ONE definition site: a `constexpr`
// `DefaultBinding` table here. `registerCameraCommands()` /
// `registerCaptureCommands()` (the neighbouring `command_suite_*.hpp` headers)
// are loops over these tables via `IRCommand::registerBindings`, so "what does
// the engine bind by default" is answerable without reading a suite body —
// from C++ via `suiteDefaults(Suite)` and from Lua via
// `IRCommand.suiteDefaults(IRCommand.Suite.CAMERA)`.
//
// This header is deliberately DATA-ONLY: it names `CommandNames` values but
// never the `Command<NAME>::create()` bodies, so enumerating the defaults does
// not drag the render / video / input command headers into scope. That is what
// lets `engine/script` expose the suites on the Lua surface. The bodies are
// resolved at registration time by `bindPrefabCommand`'s runtime dispatch.
//
// The complementary query — what IS bound right now, across every registration
// including a creation's own — is `CommandManager::getCommandRegistrations()`.
// That map is NOT a substitute for these tables: it is populated only after
// registration and only for *named PRESSED* binds, so it cannot see the
// RELEASED `MOVE_CAMERA_*_END` rows.

#include <irreden/command/ir_command_types.hpp>
#include <irreden/input/ir_input_types.hpp>

#include <span>

namespace IRCommand {

/// Identifies an engine-shipped default-binding manifest. Crosses the Lua
/// boundary as an integer (`IRCommand.Suite.CAMERA`) per
/// `.claude/rules/cpp-lua-enums.md` — never as a string name.
enum class Suite { CAMERA, CAPTURE };

/// Camera control defaults: Escape closes, `=`/`-` zoom, WASD pans.
///
/// The WASD rows come in PRESSED/RELEASED pairs because camera movement is a
/// start/stop pair of *distinct* commands (`MOVE_CAMERA_UP_START` /
/// `MOVE_CAMERA_UP_END`), not one command with two statuses. An `omit_` of
/// `MOVE_CAMERA_UP_START` therefore leaves the `_END` row bound; omit both to
/// drop the axis. A `remap_` keys on the button, so it moves the pair together.
inline constexpr DefaultBinding kCameraSuite[] = {
    {CLOSE_WINDOW, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonEscape},
    {ZOOM_IN, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonEqual},
    {ZOOM_OUT, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonMinus},
    {MOVE_CAMERA_UP_START, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonW},
    {MOVE_CAMERA_DOWN_START, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonS},
    {MOVE_CAMERA_LEFT_START, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonA},
    {MOVE_CAMERA_RIGHT_START, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonD},
    {MOVE_CAMERA_UP_END, IRInput::KEY_MOUSE, IRInput::RELEASED, IRInput::kKeyButtonW},
    {MOVE_CAMERA_DOWN_END, IRInput::KEY_MOUSE, IRInput::RELEASED, IRInput::kKeyButtonS},
    {MOVE_CAMERA_LEFT_END, IRInput::KEY_MOUSE, IRInput::RELEASED, IRInput::kKeyButtonA},
    {MOVE_CAMERA_RIGHT_END, IRInput::KEY_MOUSE, IRInput::RELEASED, IRInput::kKeyButtonD},
};

/// Screen-capture defaults: F7 canvas-only shot, F8 full-screen shot, F9
/// recording toggle.
inline constexpr DefaultBinding kCaptureSuite[] = {
    {SCREENSHOT, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonF8},
    {SCREENSHOT_CANVAS, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonF7},
    {RECORD_TOGGLE, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonF9},
};

/// The default-binding manifest @p suite would register, in registration order.
/// Reports what the engine binds *by default*, independent of what any
/// particular creation actually registered.
inline std::span<const DefaultBinding> suiteDefaults(Suite suite) {
    switch (suite) {
    case Suite::CAMERA:
        return kCameraSuite;
    case Suite::CAPTURE:
        return kCaptureSuite;
    }
    return {};
}

} // namespace IRCommand

#endif /* COMMAND_SUITE_REGISTRY_H */
