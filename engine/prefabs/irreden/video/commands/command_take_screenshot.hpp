#ifndef COMMAND_TAKE_SCREENSHOT_H
#define COMMAND_TAKE_SCREENSHOT_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_video.hpp>

namespace IRCommand {

template <> struct Command<SCREENSHOT> {
    static auto create() {
        return []() { IRVideo::requestScreenshot(); };
    }
};

} // namespace IRCommand

#endif /* COMMAND_TAKE_SCREENSHOT_H */
