/*
 * Project: Irreden Engine
 * File: renderer.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef RENDERER_H
#define RENDERER_H

#include <irreden/render/buffer.hpp>
#include <irreden/input/ir_glfw_window.hpp>

namespace IRRender {

    class Renderer {
    public:
        Renderer(IRInput::IRGLFWWindow& window);
        ~Renderer() {}

        void tick();

        void printGLSystemInfo();

    private:
        IRInput::IRGLFWWindow& m_window;
        Buffer m_bufferUniformConstantsGLSL;

        void initRenderingSystems();
    };

} // namespace IRRender

#endif /* RENDERER_H */
