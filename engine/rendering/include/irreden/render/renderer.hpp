/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\renderer.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef RENDERER_H
#define RENDERER_H

#include <irreden/render/buffer.hpp>
#include <irreden/input/ir_glfw_window.hpp>

using IRGLFW::IRGLFWWindow;

namespace IRRender {

    class Renderer {
    public:
        Renderer(IRGLFWWindow& window);
        ~Renderer() {}

        void tick();

        void printGLSystemInfo();

    private:
        IRGLFWWindow& m_window;
        Buffer m_bufferUniformConstantsGLSL;

        void initRenderingSystems();
    };

} // namespace IRRender

#endif /* RENDERER_H */
