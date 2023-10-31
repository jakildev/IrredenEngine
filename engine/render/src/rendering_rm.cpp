/*
 * Project: Irreden Engine
 * File: rendering_rm.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/render/rendering_rm.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/texture.hpp>
#include <irreden/render/framebuffer.hpp>
#include <irreden/render/vao.hpp>
#include <irreden/render/buffer.hpp>

namespace IRRender {

RenderingResourceManager::RenderingResourceManager() {
    IRProfile::engLogInfo("Initalizing rendering resource manager.");
    m_liveResourceCount = 0;
    IRProfile::engLogInfo("Creating an resource id pool. IR_MAX_RESOURCES={}", static_cast<int>(IR_MAX_RESOURCES));
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
    IRProfile::engLogInfo("Created rendering resource manager.");
}

} // namespace IRRender