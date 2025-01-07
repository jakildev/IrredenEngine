#include <irreden/render/metal/metal_render_impl.hpp>

#include <memory>

namespace IRRender {

    std::unique_ptr<RenderImpl> createRenderer() {
        return std::make_unique<MetalRenderImpl>();
    }

} // namespace IRRender