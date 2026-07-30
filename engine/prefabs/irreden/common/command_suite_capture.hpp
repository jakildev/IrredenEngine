#ifndef COMMAND_SUITE_CAPTURE_H
#define COMMAND_SUITE_CAPTURE_H

// The capture suite's bindings are declared as data in `kCaptureSuite`
// (`command_suite_registry.hpp`); this header is just the registration entry
// point. The command-body headers below are what give `bindPrefabCommand` the
// `Command<NAME>::create()` specializations it dispatches each manifest row to.

#include <irreden/common/command_suite_registry.hpp>

#include <irreden/video/commands/command_take_screenshot.hpp>
#include <irreden/video/commands/command_take_screenshot_canvas.hpp>
#include <irreden/video/commands/command_toggle_recording.hpp>

namespace IRCommand {

/// Registers the engine's default capture keys, honoring @p overrides.
/// `registerCaptureCommands({.omit_ = {RECORD_TOGGLE}})` takes the screenshot
/// keys without F9. Exact matching semantics: @ref BindingOverrides.
inline void registerCaptureCommands(const BindingOverrides &overrides) {
    registerBindings(kCaptureSuite, overrides);
}

/// Registers the engine's default capture keys unmodified.
inline void registerCaptureCommands() {
    registerBindings(kCaptureSuite);
}

} // namespace IRCommand

#endif /* COMMAND_SUITE_CAPTURE_H */
