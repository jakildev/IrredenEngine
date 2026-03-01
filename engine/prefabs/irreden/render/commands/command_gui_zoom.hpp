#ifndef COMMAND_GUI_ZOOM_H
#define COMMAND_GUI_ZOOM_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_command.hpp>

namespace IRCommand {

template <> struct Command<GUI_ZOOM_IN> {
    static auto create() {
        return []() { IRRender::setGuiScale(IRRender::getGuiScale() + 1); };
    }
};

template <> struct Command<GUI_ZOOM_OUT> {
    static auto create() {
        return []() { IRRender::setGuiScale(IRRender::getGuiScale() - 1); };
    }
};

} // namespace IRCommand

#endif /* COMMAND_GUI_ZOOM_H */
