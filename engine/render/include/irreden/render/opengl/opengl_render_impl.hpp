#pragma once

#include <irreden/ir_profile.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/render/renderer_impl.hpp>
#include <irreden/render/opengl/opengl_types.hpp>

#include <memory>

namespace IRRender {

class OpenGLRenderImpl : public RenderImpl {
  public:
    OpenGLRenderImpl() {
        IR_LOG_INFO("Initalizing OpenGL render implementation.");
    }
    void init();
    void printInfo();
};

void openGLCallback_framebuffer_size(GLFWwindow *window, int width, int height);

} // namespace IRRender