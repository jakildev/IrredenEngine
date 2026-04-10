#ifndef COMMAND_SUITE_CAPTURE_H
#define COMMAND_SUITE_CAPTURE_H

#include <irreden/video/commands/command_take_screenshot.hpp>
#include <irreden/video/commands/command_take_screenshot_canvas.hpp>
#include <irreden/video/commands/command_toggle_recording.hpp>

namespace IRCommand {

inline void registerCaptureCommands() {
    createCommand<SCREENSHOT>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonF8
    );
    createCommand<SCREENSHOT_CANVAS>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonF7
    );
    createCommand<RECORD_TOGGLE>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonF9
    );
}

} // namespace IRCommand

#endif /* COMMAND_SUITE_CAPTURE_H */
