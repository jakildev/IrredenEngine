#ifndef COMMAND_SAVE_MAIN_CANVAS_TRIXELS_H
#define COMMAND_SAVE_MAIN_CANVAS_TRIXELS_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_ecs.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>

namespace IRCommand {

template <> struct Command<SAVE_MAIN_CANVAS_TRIXELS> {
    static auto create() {
        return []() {
            auto &textures =
                IRECS::getComponent<C_TriangleCanvasTextures>(IRRender::getCanvas("main"));
            textures.saveToFile("main_canvas");
        };
    }
};

} // namespace IRCommand

#endif /* COMMAND_SAVE_MAIN_CANVAS_TRIXELS_H */
