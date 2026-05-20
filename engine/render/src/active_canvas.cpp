#include <irreden/render/active_canvas.hpp>

#include <irreden/ir_render.hpp>
#include <irreden/render/render_manager.hpp>

namespace IRRender {

IREntity::EntityId getActiveCanvasEntityOrNull() {
    return g_renderManager != nullptr ? g_renderManager->getActiveCanvasEntity()
                                      : IREntity::kNullEntity;
}

} // namespace IRRender
