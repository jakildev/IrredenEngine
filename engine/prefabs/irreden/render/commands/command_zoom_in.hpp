#ifndef COMMAND_ZOOM_IN_H
#define COMMAND_ZOOM_IN_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_command.hpp>
#include <irreden/ir_ecs.hpp>

#include <irreden/render/components/component_camera.hpp>
#include <irreden/render/components/component_zoom_level.hpp>

namespace IRCommand {

    template<>
    struct Command<ZOOM_IN> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_ZoomLevel>(
                    IRECS::getEntity("camera")
                ).zoomIn();
            };
        }
    };

} // namespace IRCommand

#endif /* COMMAND_ZOOM_IN_H */
