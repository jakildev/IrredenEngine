#pragma once

#include <irreden/ir_profile.hpp>

#include <irreden/ir_window.hpp>
#include <irreden/render/renderer_impl.hpp>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <memory>

namespace IRRender {

    void openGLCallback_framebuffer_size(GLFWwindow* window, int width, int height);

    class OpenGLRenderImpl : public RenderImpl {
    public:
        OpenGLRenderImpl() {
            IR_LOG_INFO("Initalizing OpenGL render implementation.")
        }
        void init() {
            int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
            IR_ASSERT(status, "Failed to initalize GLAD");
            IRWindow::getWindow().setCallbackFramebufferSize(
                openGLCallback_framebuffer_size
            )
        }

    };

    void openGLCallback_framebuffer_size(GLFWwindow* window, int width, int height)
    {
        glViewport(0, 0, width, height);
        IRE_LOG_INFO("Resized viewport to {}x{}", width, height);
    }

} // namespace IRRender