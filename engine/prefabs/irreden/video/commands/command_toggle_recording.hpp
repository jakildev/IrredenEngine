#ifndef COMMAND_TOGGLE_RECORDING_H
#define COMMAND_TOGGLE_RECORDING_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_video.hpp>

namespace IRCommand {

template <> struct Command<RECORD_TOGGLE> {
    static auto create() {
        return []() { IRVideo::toggleRecording(); };
    }
};

} // namespace IRCommand

#endif /* COMMAND_TOGGLE_RECORDING_H */
