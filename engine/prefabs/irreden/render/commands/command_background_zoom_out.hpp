#ifndef COMMAND_BACKGROUND_ZOOM_OUT_H
#define COMMAND_BACKGROUND_ZOOM_OUT_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_command.hpp>

namespace IRCommand {

template <> struct Command<BACKGROUND_ZOOM_OUT> {
    static auto create() {
        return []() { IRRender::zoomMainBackgroundPatternOut(); };
    }
};

} // namespace IRCommand

#endif /* COMMAND_BACKGROUND_ZOOM_OUT_H */
