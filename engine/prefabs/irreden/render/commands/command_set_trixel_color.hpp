#ifndef COMMAND_SET_TRIXEL_COLOR_H
#define COMMAND_SET_TRIXEL_COLOR_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_command.hpp>
#include <irreden/ir_ecs.hpp>

#include <irreden/render/components/component_camera.hpp>

namespace IRCommand {

    // WIP WIP WIP WIP WIP
    template<>
    struct Command<SET_TRIXEL_COLOR> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_TriangleCanvasTextures>(
                    IRRender::getCanvas("main")
                ).setTrixel(
                    IRRender::mouseTrixelPositionWorld(),
                    Color{44, 20, 200, 255}
                );
            };
        }
    };

} // namespace IRCommand

#endif /* COMMAND_SET_TRIXEL_COLOR_H */
