#include <irreden/render/opengl/opengl_render_impl.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/ir_gl_api.hpp>
#include <irreden/render/render_device.hpp>
#include <irreden/render/texture.hpp>
#include <irreden/render/opengl/opengl_types.hpp>

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace IRRender {

namespace {
struct OpenGLTimestampPair {
    GLuint startQuery_ = 0;
    GLuint endQuery_ = 0;
    bool hasStart_ = false;
    bool hasEnd_ = false;
};

class OpenGLRenderDevice final : public RenderDevice {
  public:
    void beginFrame() override {}

    void present() override {
        if (isOffscreen()) {
            // Nothing to show. A flush keeps the submission cadence of an
            // unthrottled swap (swapInterval 0) without blocking on the GPU.
            ENG_API->glFlush();
            return;
        }
        IRWindow::getWindow().swapBuffers();
    }

    // OFFSCREEN window mode: an engine-owned framebuffer stands in for the
    // default framebuffer — the same RGBA8 colour + 24/8 depth-stencil shape
    // the window's has, at its framebuffer size — so the final composite,
    // the overlays, and the screenshot readback never touch a surface the GL
    // spec leaves undefined for an unmapped window.
    void initOffscreenTarget(int width, int height) {
        releaseOffscreenTarget();
        ENG_API->glCreateTextures(GL_TEXTURE_2D, 1, &m_offscreenColor);
        ENG_API->glTextureStorage2D(m_offscreenColor, 1, GL_RGBA8, width, height);
        ENG_API->glCreateTextures(GL_TEXTURE_2D, 1, &m_offscreenDepth);
        ENG_API->glTextureStorage2D(m_offscreenDepth, 1, GL_DEPTH24_STENCIL8, width, height);
        ENG_API->glCreateFramebuffers(1, &m_offscreenFbo);
        ENG_API
            ->glNamedFramebufferTexture(m_offscreenFbo, GL_COLOR_ATTACHMENT0, m_offscreenColor, 0);
        ENG_API->glNamedFramebufferTexture(
            m_offscreenFbo,
            GL_DEPTH_STENCIL_ATTACHMENT,
            m_offscreenDepth,
            0
        );
        const GLenum status =
            ENG_API->glCheckNamedFramebufferStatus(m_offscreenFbo, GL_FRAMEBUFFER);
        IR_ASSERT(status == GL_FRAMEBUFFER_COMPLETE, "Offscreen screen target is incomplete");
    }

    void releaseOffscreenTarget() {
        if (m_offscreenFbo != 0) {
            ENG_API->glDeleteFramebuffers(1, &m_offscreenFbo);
            m_offscreenFbo = 0;
        }
        if (m_offscreenColor != 0) {
            ENG_API->glDeleteTextures(1, &m_offscreenColor);
            m_offscreenColor = 0;
        }
        if (m_offscreenDepth != 0) {
            ENG_API->glDeleteTextures(1, &m_offscreenDepth);
            m_offscreenDepth = 0;
        }
    }

    bool isOffscreen() const {
        return m_offscreenFbo != 0;
    }

    void dispatchCompute(std::uint32_t x, std::uint32_t y, std::uint32_t z) override {
        ENG_API->glDispatchCompute(x, y, z);
    }

    void dispatchComputeIndirect(const Buffer *indirectBuffer, std::ptrdiff_t offset) override {
        if (indirectBuffer == nullptr) {
            return;
        }
        ENG_API->glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, indirectBuffer->getHandle());
        ENG_API->glDispatchComputeIndirect(static_cast<GLintptr>(offset));
        ENG_API->glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    }

    void memoryBarrier(BarrierType barrierType) override {
        ENG_API->glMemoryBarrier(toGLBarrierType(barrierType));
    }

    void drawElements(DrawMode drawMode, int count, IndexType indexType) override {
        ENG_API->glDrawElements(toGLDrawMode(drawMode), count, toGLIndexType(indexType), nullptr);
    }

    void drawElementsInstanced(
        DrawMode drawMode, int count, IndexType indexType, int instanceCount
    ) override {
        ENG_API->glDrawElementsInstanced(
            toGLDrawMode(drawMode),
            count,
            toGLIndexType(indexType),
            nullptr,
            instanceCount
        );
    }

    void drawElementsInstancedIndirect(
        DrawMode drawMode,
        IndexType indexType,
        const Buffer *indirectBuffer,
        std::ptrdiff_t indirectOffset
    ) override {
        if (indirectBuffer == nullptr) {
            return;
        }
        ENG_API->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer->getHandle());
        ENG_API->glDrawElementsIndirect(
            toGLDrawMode(drawMode),
            toGLIndexType(indexType),
            reinterpret_cast<const void *>(indirectOffset)
        );
        ENG_API->glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    }

    void drawArrays(DrawMode drawMode, int first, int count) override {
        ENG_API->glDrawArrays(toGLDrawMode(drawMode), first, count);
    }

    void drawArraysInstanced(DrawMode drawMode, int first, int count, int instanceCount) override {
        if (instanceCount <= 0) {
            return;
        }
        ENG_API->glDrawArraysInstanced(toGLDrawMode(drawMode), first, count, instanceCount);
    }

    void copyImageSubData(
        std::uint32_t srcHandle,
        int srcLevel,
        int srcX,
        int srcY,
        int srcZ,
        std::uint32_t dstHandle,
        int dstLevel,
        int dstX,
        int dstY,
        int dstZ,
        int width,
        int height,
        int depth
    ) override {
        ENG_API->glCopyImageSubData(
            srcHandle,
            GL_TEXTURE_2D,
            srcLevel,
            srcX,
            srcY,
            srcZ,
            dstHandle,
            GL_TEXTURE_2D_ARRAY,
            dstLevel,
            dstX,
            dstY,
            dstZ,
            width,
            height,
            depth
        );
    }

    void setPolygonMode(PolygonMode polygonMode) override {
        ENG_API->glPolygonMode(GL_FRONT_AND_BACK, toGLPolygonMode(polygonMode));
    }

    void bindDefaultFramebuffer() override {
        ENG_API->glBindFramebuffer(GL_FRAMEBUFFER, m_offscreenFbo);
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

        ENG_API->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_offscreenFbo);
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

    void clearTexImage(const Texture2D *texture, int level, const void *data) override {
        if (texture == nullptr) {
            return;
        }
        ENG_API->glClearTexImage(texture->getHandle(), level, GL_RED_INTEGER, GL_INT, data);
    }

    void fillBuffer(const Buffer *buffer, std::size_t sizeBytes, std::uint8_t byteValue) override {
        if (buffer == nullptr || sizeBytes == 0) {
            return;
        }
        ENG_API->glClearNamedBufferSubData(
            buffer->getHandle(),
            GL_R8UI,
            0,
            static_cast<GLsizeiptr>(sizeBytes),
            GL_RED_INTEGER,
            GL_UNSIGNED_BYTE,
            &byteValue
        );
    }

    void finish() override {
        ENG_API->glFinish();
    }

    GpuTimestampHandle createTimestampPair() override {
        GLuint queries[2] = {0, 0};
        ENG_API->glGenQueries(2, queries);
        const GpuTimestampHandle handle = m_nextTimestampHandle++;
        m_timestamps[handle] = OpenGLTimestampPair{queries[0], queries[1], false, false};
        return handle;
    }

    void destroyTimestampPair(GpuTimestampHandle handle) override {
        auto it = m_timestamps.find(handle);
        if (it == m_timestamps.end()) {
            return;
        }
        GLuint queries[2] = {it->second.startQuery_, it->second.endQuery_};
        ENG_API->glDeleteQueries(2, queries);
        m_timestamps.erase(it);
    }

    bool supportsGpuTimestampPairs() const override {
        return GLAD_GL_VERSION_3_3 != 0;
    }

    void writeTimestamp(GpuTimestampHandle handle, TimestampSlot slot) override {
        auto it = m_timestamps.find(handle);
        if (it == m_timestamps.end()) {
            return;
        }
        OpenGLTimestampPair &pair = it->second;
        if (slot == TimestampSlot::START) {
            ENG_API->glQueryCounter(pair.startQuery_, GL_TIMESTAMP);
            pair.hasStart_ = true;
            pair.hasEnd_ = false;
            return;
        }
        ENG_API->glQueryCounter(pair.endQuery_, GL_TIMESTAMP);
        pair.hasEnd_ = true;
    }

    bool readTimestampPairMs(GpuTimestampHandle handle, float &outMs) override {
        return pollTimestampPairMs(handle, outMs) == TimestampReadStatus::READY;
    }

    TimestampReadStatus pollTimestampPairMs(GpuTimestampHandle handle, float &outMs) override {
        auto it = m_timestamps.find(handle);
        if (it == m_timestamps.end()) {
            return TimestampReadStatus::INVALID;
        }
        const OpenGLTimestampPair &pair = it->second;
        if (!pair.hasStart_ || !pair.hasEnd_) {
            return TimestampReadStatus::PENDING;
        }

        GLint startAvailable = GL_FALSE;
        GLint endAvailable = GL_FALSE;
        ENG_API->glGetQueryObjectiv(pair.startQuery_, GL_QUERY_RESULT_AVAILABLE, &startAvailable);
        ENG_API->glGetQueryObjectiv(pair.endQuery_, GL_QUERY_RESULT_AVAILABLE, &endAvailable);

        GLuint64 startNs = 0;
        GLuint64 endNs = 0;
        if (startAvailable == GL_TRUE && endAvailable == GL_TRUE) {
            ENG_API->glGetQueryObjectui64v(pair.startQuery_, GL_QUERY_RESULT, &startNs);
            ENG_API->glGetQueryObjectui64v(pair.endQuery_, GL_QUERY_RESULT, &endNs);
        }
        return classifyOpenGLTimestampPair(
            startAvailable == GL_TRUE,
            endAvailable == GL_TRUE,
            startNs,
            endNs,
            outMs
        );
    }

  private:
    GpuTimestampHandle m_nextTimestampHandle = 1;
    std::unordered_map<GpuTimestampHandle, OpenGLTimestampPair> m_timestamps;
    // 0 = the window's default framebuffer (every mode but OFFSCREEN).
    GLuint m_offscreenFbo = 0;
    GLuint m_offscreenColor = 0;
    GLuint m_offscreenDepth = 0;
};

OpenGLRenderDevice g_openGLRenderDevice;
} // namespace

std::unique_ptr<RenderImpl> createRenderer() {
    return std::make_unique<OpenGLRenderImpl>();
}

RenderDevice *bootstrapHeadlessRenderDevice() {
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        return nullptr;
    }
    setDevice(&g_openGLRenderDevice);
    return &g_openGLRenderDevice;
}

void OpenGLRenderImpl::init() {
    IRE_LOG_INFO("Initializing OpenGL renderer implementation.");
    auto *renderDevice = bootstrapHeadlessRenderDevice();
    IR_ASSERT(renderDevice != nullptr, "Failed to initalize GLAD");
    IRWindow::IRGLFWWindow &window = IRWindow::getWindow();
    if (window.getWindowMode() == IRWindow::WindowMode::OFFSCREEN) {
        int width = 0;
        int height = 0;
        window.getFramebufferSize(width, height);
        g_openGLRenderDevice.initOffscreenTarget(width, height);
        IRE_LOG_INFO("OpenGL offscreen screen target attached ({}x{}).", width, height);
    }
    window.setCallbackFramebufferSize(openGLCallback_framebuffer_size);
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
    if (g_openGLRenderDevice.isOffscreen()) {
        g_openGLRenderDevice.initOffscreenTarget(width, height);
    }
    ENG_API->glViewport(0, 0, width, height);
    IRE_LOG_INFO("Resized viewport to {}x{}", width, height);
}

} // namespace IRRender
