#ifndef IR_COMMAND_TYPES_H
#define IR_COMMAND_TYPES_H

#include <irreden/input/ir_input_types.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace IRCommand {

/// Opaque handle returned by `createCommand`; identifies a registered binding.
using CommandId = std::uint32_t;

/// All engine-level command identifiers.
/// Every prefab command must have an entry here **before** implementing the
/// `Command<NAME>` specialisation — missing entries cause linker errors.
enum CommandNames {
    NULL_COMMAND,
    EXAMPLE,
    ZOOM_IN,
    ZOOM_OUT,
    BACKGROUND_ZOOM_IN,
    BACKGROUND_ZOOM_OUT,
    CLOSE_WINDOW,
    MOVE_CAMERA_LEFT_START,
    MOVE_CAMERA_RIGHT_START,
    MOVE_CAMERA_UP_START,
    MOVE_CAMERA_DOWN_START,
    MOVE_CAMERA_LEFT_END,
    MOVE_CAMERA_RIGHT_END,
    MOVE_CAMERA_UP_END,
    MOVE_CAMERA_DOWN_END,
    SCREENSHOT,        // captures the full screen output (debug overlay, HUD, etc.)
    SCREENSHOT_CANVAS, // captures the main canvas framebuffer only (no overlay)
    RECORD_START,      // todo
    RECORD_STOP,       // todo
    RECORD_TOGGLE,
    STOP_VELOCITY,
    RESHAPE_SPHERE,
    RESHAPE_RECTANGULAR_PRISM,
    RANDOMIZE_VOXELS, // one-shot IRSystem::executeQuery over C_VoxelSetNew,
    // excluding C_Locked entities (#17) — see command_randomize_voxels.hpp
    LOCK_VOXEL_SCALE, // TODO: This will zoom in the rendered framebuffer,
    // and not change the on the canvas to framebuffer...
    UNLOCK_VOXEL_SCALE, // TODO: see above
    SPAWN_PARTICLE_MOUSE_POSITION,
    SET_TRIXEL_COLOR,
    TOGGLE_PERIODIC_IDLE_PAUSE,
    TOGGLE_GUI,
    GUI_ZOOM_IN,
    GUI_ZOOM_OUT,
    TOGGLE_CULLING_FREEZE,
    TOGGLE_CULLING_MINIMAP,
    TOGGLE_HELP_OVERLAY,
    TOGGLE_SETTINGS_MENU
};

/// Number of `CommandNames` values. Keeps the hand-listed `kCommandInfo`
/// table in `ir_command.hpp` honest — a new enum value without a matching
/// row is a compile error there, not a silent "UNKNOWN" at render time.
/// Update alongside the last enumerator when extending the enum.
inline constexpr int kCommandNameCount = static_cast<int>(TOGGLE_SETTINGS_MENU) + 1;

/// One declarative row of a default-binding manifest: "this command, on this
/// trigger". Pure data — a manifest never names a `Command<NAME>` body, so a
/// table of these can be declared (and enumerated) without pulling the prefab
/// command headers into scope. @ref registerBindings turns a row into a live
/// registration via the runtime `bindPrefabCommand` dispatch.
///
/// Engine-shipped manifests live in
/// `engine/prefabs/irreden/common/command_suite_registry.hpp`; a creation is
/// free to declare its own table of the same type and feed it to the same
/// registration primitive.
///
/// A row carries `requiredModifiers_` but deliberately **no blocked-modifier
/// field**, so the manifest is narrower than the primitive it feeds:
/// @ref registerBindings passes only `requiredModifiers_` through to
/// `bindPrefabCommand`, leaving its `blockedModifiers` at the default. A
/// binding that must be *suppressed* while a modifier is held stays a
/// hand-written `bindPrefabCommand` call rather than a manifest row.
struct DefaultBinding {
    CommandNames command_;
    IRInput::InputTypes inputType_;
    IRInput::ButtonStatuses status_;
    int button_;
    IRInput::KeyModifierMask requiredModifiers_ = IRInput::kModifierNone;
};

/// Registration-time customization of a @ref DefaultBinding manifest, applied
/// by @ref registerBindings.
///
/// - `omit_` drops every row whose `command_` appears in the list. Matching is
///   by command identity, so a command bound on several rows (the camera
///   suite's PRESSED + RELEASED pairs are *distinct* commands) drops only the
///   rows naming that exact command.
/// - `remap_` substitutes buttons as `{from, to}` pairs, applied to `button_`
///   after the omit filter. A remap matches on the button, so it moves **every**
///   row using that button — `{kKeyButtonW, kKeyButtonUp}` carries both
///   `MOVE_CAMERA_UP_START` (PRESSED) and `MOVE_CAMERA_UP_END` (RELEASED)
///   across together. At most one substitution is applied per row (the first
///   matching pair wins), so remaps never chain.
///
/// Deliberately registration-time rather than a mutable unbind/rebind registry:
/// `CommandId` is an index into an append-only vector, and `CommandNames` is
/// not a unique key in the live registry — see `engine/command/CLAUDE.md`
/// §"Default-binding manifests".
struct BindingOverrides {
    std::vector<CommandNames> omit_;
    std::vector<std::pair<int, int>> remap_;
};

/// Type-tag struct; specialised per command to provide a `create()` factory
/// that returns the `std::function<void()>` for that command.
template <CommandNames command> struct Command;

/// Internal binding category used by `CommandManager` to route events to the
/// right registry (button, MIDI note, or MIDI CC).
enum CommandTypes { IR_COMMAND_NULL, COMMAND_BUTTON, COMMAND_MIDI_NOTE, COMMAND_MIDI_CC };

} // namespace IRCommand

#endif /* IR_COMMAND_TYPES_H */
