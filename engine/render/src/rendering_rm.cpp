#include <irreden/ir_render.hpp>

#include <irreden/render/rendering_rm.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/texture.hpp>
#include <irreden/render/framebuffer.hpp>
#include <irreden/render/vao.hpp>
#include <irreden/render/buffer.hpp>

namespace IRRender {

RenderingResourceManager::RenderingResourceManager() {
    m_liveResourceCount = 0;
    IRE_LOG_INFO(
        "Creating an resource id pool. IR_MAX_RESOURCES={}",
        static_cast<int>(IR_MAX_RESOURCES)
    );
    for (ResourceId resource = 0; resource < IR_MAX_RESOURCES; resource++) {
        m_resourcePool.push(resource);
    }
    registerResource<ShaderStage>();
    registerResource<Texture2D>();
    registerResource<Framebuffer>();
    registerResource<ShaderProgram>();
    registerResource<Buffer>();
    registerResource<VAO>();

    g_renderingResourceManager = this;
    IRE_LOG_INFO("Created RenderingResourceManager");
}

RenderingResourceManager::~RenderingResourceManager() {
    if (g_renderingResourceManager == this) {
        g_renderingResourceManager = nullptr;
    }
}

} // namespace IRRender