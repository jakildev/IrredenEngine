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

        void printInfo() {
            int intAttr;
            ENG_API->glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &intAttr);
            IRE_LOG_INFO(
                "Maximum nr of vertex attributes supported: {}",
                intAttr
            );
            ENG_API->glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &intAttr);
            IRE_LOG_INFO(
                "Max 3d texture size: {}",
                intAttr
            );
            ENG_API->glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &intAttr);
            IRE_LOG_INFO(
                "Max uniform block size: {}",
                intAttr
            );
        }   



    };

    void openGLCallback_framebuffer_size(GLFWwindow* window, int width, int height)
    {
        glViewport(0, 0, width, height);
        IRE_LOG_INFO("Resized viewport to {}x{}", width, height);
    }

} // namespace IRRender