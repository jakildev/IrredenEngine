#include <irreden/ir_render.hpp>
#include <irreden/render/rendering_rm.hpp>
#include <irreden/ir_profile.hpp>

namespace IRRender {

    RenderingResourceManager* g_renderingResourceManager = nullptr;
    RenderingResourceManager& getRenderingResourceManager() {
        IR_ENG_ASSERT(
            g_renderingResourceManager != nullptr,
            "RenderingResourceManager not initalized"
        );
        return *g_renderingResourceManager;
    }

} // namespace IRRender

