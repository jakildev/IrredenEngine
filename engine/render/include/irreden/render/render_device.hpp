#ifndef RENDER_DEVICE_H
#define RENDER_DEVICE_H

#include <irreden/render/ir_render_enums.hpp>

#include <cstdint>

namespace IRRender {

class RenderDevice {
  public:
    virtual ~RenderDevice() = default;

    virtual void beginFrame() = 0;
    virtual void present() = 0;

    virtual void dispatchCompute(std::uint32_t x, std::uint32_t y, std::uint32_t z) = 0;
    virtual void memoryBarrier(BarrierType barrierType) = 0;
    virtual void drawElements(DrawMode drawMode, int count, IndexType indexType) = 0;
    virtual void drawArrays(DrawMode drawMode, int first, int count) = 0;
    virtual void setPolygonMode(PolygonMode polygonMode) = 0;
    virtual void bindDefaultFramebuffer() = 0;
    virtual void clearDefaultFramebuffer() = 0;
    virtual bool readDefaultFramebuffer(int x, int y, int width, int height, void *rgbaData) = 0;
    virtual void enableBlending() = 0;
    virtual void disableBlending() = 0;
    virtual void setDepthTest(bool enabled) = 0;
    virtual void setDepthWrite(bool enabled) = 0;
};

RenderDevice *device();
void setDevice(RenderDevice *renderDevice);

} // namespace IRRender

#endif /* RENDER_DEVICE_H */
