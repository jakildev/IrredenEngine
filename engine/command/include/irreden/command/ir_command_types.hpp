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
