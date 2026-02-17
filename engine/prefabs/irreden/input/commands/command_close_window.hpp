#ifndef COMMAND_CLOSE_WINDOW_H
#define COMMAND_CLOSE_WINDOW_H

#include <irreden/ir_input.hpp>

#include <irreden/command/ir_command_types.hpp>

namespace IRCommand {

template <> struct Command<CLOSE_WINDOW> {
    static auto create() {
        return []() { IRWindow::closeWindow(); };
    }
};
} // namespace IRCommand

#endif /* COMMAND_CLOSE_WINDOW_H */
