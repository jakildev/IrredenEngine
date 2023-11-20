#ifndef COMMAND_ZOOM_OUT_H
#define COMMAND_ZOOM_OUT_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_command.hpp>
#include <irreden/ir_ecs.hpp>

#include <irreden/render/components/component_camera.hpp>

namespace IRCommand {

    template<>
    struct Command<ZOOM_OUT> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Camera>(
                    IRECS::getEntity("camera")
                ).zoomOut();
            };
        }
    };

} // namespace IRCommand

#endif /* COMMAND_ZOOM_OUT_H */
