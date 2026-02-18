#ifndef IR_COMMAND_TYPES_H
#define IR_COMMAND_TYPES_H

#include <cstdint>

namespace IRCommand {

using CommandId = std::uint32_t;

enum CommandNames {
    NULL_COMMAND,
    EXAMPLE,
    ZOOM_IN,
    ZOOM_OUT,
    CLOSE_WINDOW,
    MOVE_CAMERA_LEFT_START,
    MOVE_CAMERA_RIGHT_START,
    MOVE_CAMERA_UP_START,
    MOVE_CAMERA_DOWN_START,
    MOVE_CAMERA_LEFT_END,
    MOVE_CAMERA_RIGHT_END,
    MOVE_CAMERA_UP_END,
    MOVE_CAMERA_DOWN_END,
    SCREENSHOT,   // todo
    RECORD_START, // todo
    RECORD_STOP,  // todo
    RECORD_TOGGLE,
    STOP_VELOCITY,
    RESHAPE_SPHERE,
    RESHAPE_RECTANGULAR_PRISM,
    RANDOMIZE_VOXELS, // TODO, should operate like a system with
    // a query and stuff, and should exclude "locked entities"
    LOCK_VOXEL_SCALE, // TODO: This will zoom in the rendered framebuffer,
    // and not change the on the canvas to framebuffer...
    UNLOCK_VOXEL_SCALE, // TODO: see above
    SPAWN_PARTICLE_MOUSE_POSITION,
    SAVE_MAIN_CANVAS_TRIXELS,
    SET_TRIXEL_COLOR

};

template <CommandNames command> struct Command;

enum CommandTypes { IR_COMMAND_NULL, COMMAND_BUTTON, COMMAND_MIDI_NOTE, COMMAND_MIDI_CC };

} // namespace IRCommand

#endif /* IR_COMMAND_TYPES_H */
