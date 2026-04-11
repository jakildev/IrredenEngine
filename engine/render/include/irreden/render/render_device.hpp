#ifndef RENDER_DEVICE_H
#define RENDER_DEVICE_H

#include <irreden/render/ir_render_enums.hpp>

#include <cstdint>

namespace IRRender {

class Buffer;
class Texture2D;

class RenderDevice {
  public:
    virtual ~RenderDevice() = default;

    virtual void beginFrame() = 0;
    virtual void present() = 0;

    virtual void dispatchCompute(std::uint32_t x, std::uint32_t y, std::uint32_t z) = 0;
    virtual void dispatchComputeIndirect(const Buffer *indirectBuffer, std::ptrdiff_t offset) = 0;
    virtual void memoryBarrier(BarrierType barrierType) = 0;
    virtual void drawElements(DrawMode drawMode, int count, IndexType indexType) = 0;
    virtual void drawElementsInstanced(DrawMode drawMode, int count, IndexType indexType, int instanceCount) = 0;
    virtual void drawArrays(DrawMode drawMode, int first, int count) = 0;
    virtual void copyImageSubData(
        std::uint32_t srcHandle, int srcLevel, int srcX, int srcY, int srcZ,
        std::uint32_t dstHandle, int dstLevel, int dstX, int dstY, int dstZ,
        int width, int height, int depth
    ) = 0;
    virtual void setPolygonMode(PolygonMode polygonMode) = 0;
    virtual void bindDefaultFramebuffer() = 0;
    virtual void clearDefaultFramebuffer() = 0;
    virtual bool readDefaultFramebuffer(int x, int y, int width, int height, void *rgbaData) = 0;
    virtual void enableBlending() = 0;
    virtual void disableBlending() = 0;
    virtual void setDepthTest(bool enabled) = 0;
    virtual void setDepthWrite(bool enabled) = 0;
    virtual void clearTexImage(const Texture2D *texture, int level, const void *data) = 0;
    virtual void finish() = 0;
};

RenderDevice *device();
void setDevice(RenderDevice *renderDevice);

} // namespace IRRender

#endif /* RENDER_DEVICE_H */
