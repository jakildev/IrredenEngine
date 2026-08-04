#ifndef COMMAND_SUITE_CAMERA_H
#define COMMAND_SUITE_CAMERA_H

// The camera suite's bindings are declared as data in `kCameraSuite`
// (`command_suite_registry.hpp`); this header is just the registration entry
// point. The command-body headers below are what give `bindPrefabCommand` the
// `Command<NAME>::create()` specializations it dispatches each manifest row to.

#include <irreden/common/command_suite_registry.hpp>

#include <irreden/input/commands/command_close_window.hpp>
#include <irreden/render/commands/command_zoom_in.hpp>
#include <irreden/render/commands/command_zoom_out.hpp>
#include <irreden/render/commands/command_move_camera.hpp>

namespace IRCommand {

/// Registers the engine's default camera controls, honoring @p overrides.
///
/// `registerCameraCommands({.omit_ = {CLOSE_WINDOW}})` binds everything except
/// Escape — the shape a creation that owns its own Escape handling wants,
/// instead of hand-copying the rest of the suite. `IRPrefab::SettingsMenu` is
/// the in-engine case (#2551): it binds Escape to open the menu and puts a QUIT
/// button inside, so quitting stays two inputs away.
/// `registerCameraCommands({.remap_ = {{IRInput::kKeyButtonW, IRInput::kKeyButtonUp}, …}})`
/// moves pan onto the arrow keys, carrying each key's PRESSED + RELEASED rows
/// together. Exact matching semantics: @ref BindingOverrides.
inline void registerCameraCommands(const BindingOverrides &overrides) {
    registerBindings(kCameraSuite, overrides);
}

/// Registers the engine's default camera controls unmodified.
inline void registerCameraCommands() {
    registerBindings(kCameraSuite);
}

} // namespace IRCommand

#endif /* COMMAND_SUITE_CAMERA_H */
