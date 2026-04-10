#ifndef COMMAND_SUITE_CAMERA_H
#define COMMAND_SUITE_CAMERA_H

#include <irreden/input/commands/command_close_window.hpp>
#include <irreden/render/commands/command_zoom_in.hpp>
#include <irreden/render/commands/command_zoom_out.hpp>
#include <irreden/render/commands/command_move_camera.hpp>

namespace IRCommand {

inline void registerCameraCommands() {
    createCommand<CLOSE_WINDOW>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonEscape
    );
    createCommand<ZOOM_IN>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonEqual
    );
    createCommand<ZOOM_OUT>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonMinus
    );
    createCommand<MOVE_CAMERA_UP_START>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonW
    );
    createCommand<MOVE_CAMERA_DOWN_START>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonS
    );
    createCommand<MOVE_CAMERA_LEFT_START>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonA
    );
    createCommand<MOVE_CAMERA_RIGHT_START>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonD
    );
    createCommand<MOVE_CAMERA_UP_END>(
        InputTypes::KEY_MOUSE, ButtonStatuses::RELEASED,
        KeyMouseButtons::kKeyButtonW
    );
    createCommand<MOVE_CAMERA_DOWN_END>(
        InputTypes::KEY_MOUSE, ButtonStatuses::RELEASED,
        KeyMouseButtons::kKeyButtonS
    );
    createCommand<MOVE_CAMERA_LEFT_END>(
        InputTypes::KEY_MOUSE, ButtonStatuses::RELEASED,
        KeyMouseButtons::kKeyButtonA
    );
    createCommand<MOVE_CAMERA_RIGHT_END>(
        InputTypes::KEY_MOUSE, ButtonStatuses::RELEASED,
        KeyMouseButtons::kKeyButtonD
    );
}

} // namespace IRCommand

#endif /* COMMAND_SUITE_CAMERA_H */
