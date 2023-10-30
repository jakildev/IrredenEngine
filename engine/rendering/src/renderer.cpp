/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\renderer.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/render/renderer.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/ir_gl_api.hpp>
#include <irreden/ir_ecs.hpp>

#include <irreden/update/systems/system_update_screen_view.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>

using IRECS::SystemManager;

// renderer should use delta time for updating!!!

namespace IRRender {

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
        IRProfile::engLogInfo("Created renderer.");
    }

    void Renderer::tick() {
        IRProfile::profileFunction(IR_PROFILER_COLOR_RENDER);

        IRECS::getSystemManager().get<INPUT_KEY_MOUSE>()->beginRenderExecute(); // TODO: not like this after system CRTP
        IRECS::getSystemManager().get<SCREEN_VIEW>()->beginExecuteRender();

        IRECS::getSystemManager().executeGroup<SYSTEM_TYPE_RENDER>();
        m_window.swapBuffers();
    }

    void Renderer::initRenderingSystems() {

    }

    void Renderer::printGLSystemInfo() {
        int intAttr;
        ENG_API->glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &intAttr);
        IRProfile::engLogInfo(
            "Maximum nr of vertex attributes supported: {}",
            intAttr
        );
        ENG_API->glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &intAttr);
        IRProfile::engLogInfo(
            "Max 3d texture size: {}",
            intAttr
        );
        ENG_API->glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &intAttr);
        IRProfile::engLogInfo(
            "Max uniform block size: {}",
            intAttr
        );
    }

} // namespace IRRender