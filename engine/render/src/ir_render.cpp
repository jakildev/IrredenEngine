#include <irreden/ir_render.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/rendering_rm.hpp>

namespace IRRender {

    RenderingResourceManager* g_renderingResourceManager = nullptr;
    RenderingResourceManager& getRenderingResourceManager() {
        IR_ASSERT(
            g_renderingResourceManager != nullptr,
            "RenderingResourceManager not initalized"
        );
        return *g_renderingResourceManager;
    }

    RenderManager* g_renderManager = nullptr;
    RenderManager& getRenderManager() {
        IR_ASSERT(
            g_renderManager != nullptr,
            "RenderManager not initalized"
        );
        return *g_renderManager;
    }

    vec2 getCameraPositionScreen() {
        return getRenderManager().getCameraPositionScreen();
    }
    vec2 getCameraZoom() {
        return getRenderManager().getCameraZoom();
    }
    vec2 getTriangleStepSizeScreen() {
        return getRenderManager().getTriangleStepSizeScreen();
    }
    ivec2 getViewport() {
        return getRenderManager().getViewport();
    }
    int getOutputScaleFactor() {
        return getRenderManager().getOutputScaleFactor();
    }

} // namespace IRRender

