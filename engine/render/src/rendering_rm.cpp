#include <irreden/ir_render.hpp>

#include <irreden/render/rendering_rm.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/texture.hpp>
#include <irreden/render/framebuffer.hpp>
#include <irreden/render/vao.hpp>
#include <irreden/render/buffer.hpp>

namespace IRRender {

RenderingResourceManager::RenderingResourceManager(ResourceId idCapacity) {
    m_liveResourceCount = 0;
    IRE_LOG_INFO("Creating an resource id pool. idCapacity={}", static_cast<int>(idCapacity));
    for (ResourceId resource = 0; resource < idCapacity; resource++) {
        m_resourcePool.push(resource);
    }
    registerResource<ShaderStage>();
    registerResource<Texture2D>();
    registerResource<Texture3D>();
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