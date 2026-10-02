#include <irreden/render/active_canvas.hpp>

#include <irreden/ir_render.hpp>
#include <irreden/render/render_manager.hpp>

namespace IRRender {

namespace {
IREntity::EntityId g_headlessActiveCanvas = IREntity::kNullEntity;
}

IREntity::EntityId getActiveCanvasEntityOrNull() {
    return g_renderManager != nullptr ? g_renderManager->getActiveCanvasEntity()
                                      : g_headlessActiveCanvas;
}

void setHeadlessActiveCanvasEntity(IREntity::EntityId canvas) {
    g_headlessActiveCanvas = canvas;
}

} // namespace IRRender
