#ifndef COMMAND_TAKE_SCREENSHOT_CANVAS_H
#define COMMAND_TAKE_SCREENSHOT_CANVAS_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_video.hpp>

namespace IRCommand {

template <> struct Command<SCREENSHOT_CANVAS> {
    static auto create() {
        return []() { IRVideo::requestCanvasScreenshot(); };
    }
};

} // namespace IRCommand

#endif /* COMMAND_TAKE_SCREENSHOT_CANVAS_H */
