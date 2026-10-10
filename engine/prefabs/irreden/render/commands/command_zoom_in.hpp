#ifndef COMMAND_ZOOM_IN_H
#define COMMAND_ZOOM_IN_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/render/camera.hpp>

namespace IRCommand {

template <> struct Command<ZOOM_IN> {
    static auto create() {
        return []() { IRPrefab::Camera::zoomIn(); };
    }
};

} // namespace IRCommand

#endif /* COMMAND_ZOOM_IN_H */
