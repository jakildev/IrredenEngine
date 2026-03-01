#ifndef COMMAND_TOGGLE_GUI_H
#define COMMAND_TOGGLE_GUI_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_render.hpp>

namespace IRCommand {

template <> struct Command<TOGGLE_GUI> {
    static auto create() {
        return []() { IRRender::toggleGuiVisible(); };
    }
};

} // namespace IRCommand

#endif /* COMMAND_TOGGLE_GUI_H */
