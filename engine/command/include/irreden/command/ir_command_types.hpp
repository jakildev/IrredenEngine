#ifndef IR_COMMAND_TYPES_H
#define IR_COMMAND_TYPES_H

#include <cstdint>

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
    TOGGLE_CULLING_MINIMAP

};

/// Type-tag struct; specialised per command to provide a `create()` factory
/// that returns the `std::function<void()>` for that command.
template <CommandNames command> struct Command;

/// Internal binding category used by `CommandManager` to route events to the
/// right registry (button, MIDI note, or MIDI CC).
enum CommandTypes { IR_COMMAND_NULL, COMMAND_BUTTON, COMMAND_MIDI_NOTE, COMMAND_MIDI_CC };

} // namespace IRCommand

#endif /* IR_COMMAND_TYPES_H */
