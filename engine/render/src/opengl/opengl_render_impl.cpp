#include <irreden/render/opengl/opengl_render_impl.hpp>
#include <irreden/render/ir_gl_api.hpp>

namespace IRRender {

    std::unique_ptr<RenderImpl> createRenderer() {
        return std::make_unique<OpenGLRenderImpl>();
    }

    void OpenGLRenderImpl::init() {
        IRE_LOG_INFO("Initializing OpenGL renderer implementation.");
        int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
        IR_ASSERT(status, "Failed to initalize GLAD");
        IRWindow::getWindow().setCallbackFramebufferSize(
            openGLCallback_framebuffer_size
        );
    }

     void OpenGLRenderImpl::printInfo() {
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

    void openGLCallback_framebuffer_size(GLFWwindow* window, int width, int height)
    {
        ENG_API->glViewport(0, 0, width, height);
        IRE_LOG_INFO("Resized viewport to {}x{}", width, height);
    }

} // namespace IRRender
