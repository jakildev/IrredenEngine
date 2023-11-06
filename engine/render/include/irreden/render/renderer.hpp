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

#include <irreden/ir_input.hpp>

#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/buffer.hpp>

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
