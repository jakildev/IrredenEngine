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
    MetalRenderImpl();
    ~MetalRenderImpl() override;
    void init() override;
    void printInfo() override;

  private:
    MTL::Device *m_device = nullptr;
    CA::MetalLayer *m_layer = nullptr;
};

} // namespace IRRender
