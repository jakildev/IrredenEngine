/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\renderer.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include "renderer.hpp"
#include "ir_rendering.hpp"
#include "../world/global.hpp"
#include "../rendering/ir_gl_api.hpp"
#include "../ecs/system_manager.hpp"
#include "../ecs/ir_system.hpp"

#include "../systems/system_update_screen_view.hpp"
#include "../systems/system_input_key_mouse.hpp"

using IRECS::SystemManager;

// renderer should use delta time for updating!!!

namespace IRRendering {

    Renderer::Renderer(
        IRGLFWWindow& window
    )
    :   m_window{window}
    ,   m_bufferUniformConstantsGLSL{
            &kGlobalConstantsGLSL,
            sizeof(GlobalConstantsGLSL),
            GL_NONE,
            GL_UNIFORM_BUFFER,
            kBufferIndex_GlobalConstantsGLSL
        }
    {
        initRenderingSystems();
        global.renderer_ = this;
        ENG_LOG_INFO("Created renderer.");
    }

    void Renderer::tick() {
        EASY_FUNCTION(IR_PROFILER_COLOR_RENDER);

        global.systemManager_->get<INPUT_KEY_MOUSE>()->beginRenderExecute(); // TODO: not like this after system CRTP
        global.systemManager_->get<SCREEN_VIEW>()->beginExecuteRender();

        global.systemManager_->executeGroup<SYSTEM_TYPE_RENDER>();
        m_window.swapBuffers();
    }

    void Renderer::initRenderingSystems() {

    }

} // namespace IRRendering