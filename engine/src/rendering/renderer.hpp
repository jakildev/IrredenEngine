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

#include "buffer.hpp"
#include "../world/glfw_helper.hpp"

using IRGLFW::IRGLFWWindow;

namespace IRRendering {

    class Renderer {
    public:
        Renderer(IRGLFWWindow& window);
        ~Renderer() {}

        void tick();

    private:
        IRGLFWWindow& m_window;
        Buffer m_bufferUniformConstantsGLSL;

        void initRenderingSystems();
    };

} // namespace IRRendering

#endif /* RENDERER_H */
