#include <irreden/ir_render.hpp>
#include <irreden/render/rendering_rm.hpp>

namespace IRRender {
    RenderingResourceManager& getRenderingResourceManager() {
        return RenderingResourceManager::instance();
    }

} // namespace IRRender

