#pragma once

#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/metal/metal_types.hpp>

#include <irreden/ir_profile.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/render/renderer_impl.hpp>

#include <GLFW/glfw3.h>

#include <memory>

namespace IRRender {

void metalCallback_framebuffer_size(GLFWwindow *window, int width, int height);

class MetalRenderImpl : public RenderImpl {
  public:
    MetalRenderImpl() : m_device{MTL::CreateSystemDefaultDevice()} {

        IR_LOG_INFO("Initalizing Metal render implementation...");

        // ADD STUFF HERE

        IR_LOG_INFO("Initalized Metal render implementation.");
    }

    void init() {
        // int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
        // IR_ASSERT(status, "Failed to initalize GLAD");
        IRWindow::getWindow().setCallbackFramebufferSize(metalCallback_framebuffer_size);
    }

  private:
    MTL::Device *m_device;
};

void metalCallback_framebuffer_size(GLFWwindow *window, int width, int height) {
    // TODO: What here?
    IRE_LOG_INFO("Resized viewport to {}x{}", width, height);
}

} // namespace IRRender