#ifndef COMMAND_BACKGROUND_ZOOM_IN_H
#define COMMAND_BACKGROUND_ZOOM_IN_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_command.hpp>

namespace IRCommand {

template <> struct Command<BACKGROUND_ZOOM_IN> {
    static auto create() {
        return []() { IRRender::zoomMainBackgroundPatternIn(); };
    }
};

} // namespace IRCommand

#endif /* COMMAND_BACKGROUND_ZOOM_IN_H */
