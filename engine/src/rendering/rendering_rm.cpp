/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\rendering_rm.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include "../world/global.hpp"
#include "rendering_rm.hpp"
#include "shader.hpp"
#include "texture.hpp"
#include "framebuffer.hpp"
#include "vao.hpp"
#include "buffer.hpp"

namespace IRRendering {

RenderingResourceManager::RenderingResourceManager() {
    ENG_LOG_INFO("Initalizing rendering resource manager.");
    m_liveResourceCount = 0;
    ENG_LOG_INFO("Creating an resource id pool. IR_MAX_RESOURCES={}", static_cast<int>(IR_MAX_RESOURCES));
    for (ResourceId resource = 0; resource < IR_MAX_RESOURCES; resource++) {
        m_resourcePool.push(resource);
    }
    registerResource<ShaderStage>();
    registerResource<Texture2D>();
    registerResource<Framebuffer>();
    registerResource<ShaderProgram>();
    registerResource<Buffer>();
    registerResource<VAO>();

    ENG_LOG_INFO("Created rendering resource manager.");

    global.renderingResourceManager_ = this;
}

} // namespace IRRendering