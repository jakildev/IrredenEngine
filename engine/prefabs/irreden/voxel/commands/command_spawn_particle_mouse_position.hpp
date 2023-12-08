#ifndef COMMAND_SPAWN_PARTICLE_MOUSE_POSITION_H
#define COMMAND_SPAWN_PARTICLE_MOUSE_POSITION_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

namespace IRCommand {

    template<>
    struct Command<SPAWN_PARTICLE_MOUSE_POSITION> {
        static auto create() {
            return []() {
                vec2 mouseTriangleIndexMainCanvas =
                    IRRender::mousePositionScreenToMainCanvasTriangleIndex(
                        IRInput::getMousePositionUpdate()
                    );
                IRE_LOG_INFO(
                    "Mouse triangleIndex: {}, {}",
                    mouseTriangleIndexMainCanvas.x,
                    mouseTriangleIndexMainCanvas.y
                );
            };
        }
    };

} // namespace IRCommand

#endif /* COMMAND_SPAWN_PARTICLE_MOUSE_POSITION_H */
