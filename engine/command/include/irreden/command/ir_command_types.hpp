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
        SCREENSHOT, // todo
        RECORD_START, // todo
        RECORD_STOP, // todo
        STOP_VELOCITY,
        RESHAPE_SPHERE,
        RESHAPE_RECTANGULAR_PRISM,
        NUM_COMMANDS
    };

    template <CommandNames command>
    struct Command;

    enum CommandTypes {
        IR_COMMAND_NULL,
        IR_COMMAND_USER,
        IR_COMMAND_MIDI_NOTE,
        IR_COMMAND_MIDI_CC
    };



} // namespace IRCommand

#endif /* IR_COMMAND_TYPES_H */
