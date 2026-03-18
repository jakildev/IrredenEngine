#include <irreden/render/opengl/opengl_render_impl.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/ir_gl_api.hpp>
#include <irreden/render/render_device.hpp>
#include <irreden/render/opengl/opengl_types.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

namespace IRRender {

namespace {
class OpenGLRenderDevice final : public RenderDevice {
  public:
    void beginFrame() override {}

    void present() override {
        IRWindow::getWindow().swapBuffers();
    }

    void dispatchCompute(std::uint32_t x, std::uint32_t y, std::uint32_t z) override {
        glDispatchCompute(x, y, z);
    }

    void memoryBarrier(BarrierType barrierType) override {
        glMemoryBarrier(toGLBarrierType(barrierType));
    }

    void drawElements(DrawMode drawMode, int count, IndexType indexType) override {
        ENG_API->glDrawElements(toGLDrawMode(drawMode), count, toGLIndexType(indexType), nullptr);
    }

    void drawArrays(DrawMode drawMode, int first, int count) override {
        ENG_API->glDrawArrays(toGLDrawMode(drawMode), first, count);
    }

    void setPolygonMode(PolygonMode polygonMode) override {
        ENG_API->glPolygonMode(GL_FRONT_AND_BACK, toGLPolygonMode(polygonMode));
    }

    void bindDefaultFramebuffer() override {
        ENG_API->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        ENG_API->glViewport(0, 0, IRRender::getViewport().x, IRRender::getViewport().y);
        ENG_API->glEnable(GL_DEPTH_TEST);
        ENG_API->glDepthFunc(GL_LESS);
    }

    void clearDefaultFramebuffer() override {
        ENG_API->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        ENG_API->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    bool readDefaultFramebuffer(int x, int y, int width, int height, void *rgbaData) override {
        if (rgbaData == nullptr || width <= 0 || height <= 0) {
            return false;
        }

        ENG_API->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        ENG_API->glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgbaData);

        const std::size_t rowBytes = static_cast<std::size_t>(width) * 4U;
        auto *pixels = static_cast<std::uint8_t *>(rgbaData);
        std::vector<std::uint8_t> rowBuffer(rowBytes);
        for (int row = 0; row < height / 2; ++row) {
            auto *topRow = pixels + static_cast<std::size_t>(row) * rowBytes;
            auto *bottomRow = pixels + static_cast<std::size_t>(height - 1 - row) * rowBytes;
            std::memcpy(rowBuffer.data(), topRow, rowBytes);
            std::memcpy(topRow, bottomRow, rowBytes);
            std::memcpy(bottomRow, rowBuffer.data(), rowBytes);
        }

        return true;
    }

    void enableBlending() override {
        ENG_API->glEnable(GL_BLEND);
        ENG_API->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    void disableBlending() override {
        ENG_API->glDisable(GL_BLEND);
    }

    void setDepthTest(bool enabled) override {
        if (enabled) {
            ENG_API->glEnable(GL_DEPTH_TEST);
        } else {
            ENG_API->glDisable(GL_DEPTH_TEST);
        }
    }

    void setDepthWrite(bool enabled) override {
        ENG_API->glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    }
};

OpenGLRenderDevice g_openGLRenderDevice;
} // namespace

std::unique_ptr<RenderImpl> createRenderer() {
    return std::make_unique<OpenGLRenderImpl>();
}

void OpenGLRenderImpl::init() {
    IRE_LOG_INFO("Initializing OpenGL renderer implementation.");
    int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    IR_ASSERT(status, "Failed to initalize GLAD");
    setDevice(&g_openGLRenderDevice);
    IRWindow::getWindow().setCallbackFramebufferSize(openGLCallback_framebuffer_size);
}

void OpenGLRenderImpl::printInfo() {
    int intAttr;
    ENG_API->glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &intAttr);
    IRE_LOG_INFO("Maximum nr of vertex attributes supported: {}", intAttr);
    ENG_API->glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &intAttr);
    IRE_LOG_INFO("Max 3d texture size: {}", intAttr);
    ENG_API->glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &intAttr);
    IRE_LOG_INFO("Max uniform block size: {}", intAttr);
}

void openGLCallback_framebuffer_size(GLFWwindow *window, int width, int height) {
    ENG_API->glViewport(0, 0, width, height);
    IRE_LOG_INFO("Resized viewport to {}x{}", width, height);
}

} // namespace IRRender
