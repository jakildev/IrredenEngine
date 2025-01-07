#include <irreden/render/opengl/opengl_render_impl.hpp>

namespace IRRender {

    std::unique_ptr<RenderImpl> createRenderer() {
        return std::make_unique<OpenGLRenderImpl>();
    }

} // namespace IRRender
