#ifndef RENDER_DEVICE_H
#define RENDER_DEVICE_H

#include <irreden/render/ir_render_enums.hpp>

#include <cstddef>
#include <cstdint>

namespace IRRender {

class Buffer;
class Texture2D;

using GpuTimestampHandle = std::uint32_t;
constexpr GpuTimestampHandle kInvalidGpuTimestampHandle = 0;

enum class TimestampSlot {
    START,
    END,
};

class RenderDevice {
  public:
    virtual ~RenderDevice() = default;

    virtual void beginFrame() = 0;
    virtual void present() = 0;

    virtual void dispatchCompute(std::uint32_t x, std::uint32_t y, std::uint32_t z) = 0;
    virtual void dispatchComputeIndirect(const Buffer *indirectBuffer, std::ptrdiff_t offset) = 0;
    virtual void memoryBarrier(BarrierType barrierType) = 0;
    virtual void drawElements(DrawMode drawMode, int count, IndexType indexType) = 0;
    virtual void
    drawElementsInstanced(DrawMode drawMode, int count, IndexType indexType, int instanceCount) = 0;
    // Indirect instanced indexed draw: index/instance counts come from a
    // DrawElementsIndirectCommand (GL) / MTLDrawIndexedPrimitivesIndirectArguments
    // (Metal) at @p indirectOffset in @p indirectBuffer, GPU-written by a compute
    // pass the same frame (#1961 per-axis cell compaction). Symmetric to
    // dispatchComputeIndirect; the bound element buffer supplies the indices.
    virtual void drawElementsInstancedIndirect(
        DrawMode drawMode,
        IndexType indexType,
        const Buffer *indirectBuffer,
        std::ptrdiff_t indirectOffset
    ) = 0;
    virtual void drawArrays(DrawMode drawMode, int first, int count) = 0;
    virtual void
    drawArraysInstanced(DrawMode drawMode, int first, int count, int instanceCount) = 0;
    virtual void copyImageSubData(
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
    // GPU-side repeating-single-byte fill of a buffer's first @p sizeBytes —
    // the semantics Metal's blit fillBuffer offers natively, matched on GL by
    // glClearNamedBufferSubData with an R8UI pattern. Suited to sentinels whose
    // every byte is identical (0x00, 0xFF); a multi-byte pattern like
    // 0x0000FFFF needs a clear dispatch instead (see the GPU-buffer-sentinel
    // gotcha in engine/prefabs/CLAUDE.md).
    virtual void
    fillBuffer(const Buffer *buffer, std::size_t sizeBytes, std::uint8_t byteValue) = 0;
    virtual void finish() = 0;

    virtual GpuTimestampHandle createTimestampPair() {
        return kInvalidGpuTimestampHandle;
    }
    virtual void destroyTimestampPair(GpuTimestampHandle) {}
    virtual bool supportsGpuTimestampPairs() const {
        return false;
    }
    virtual int recommendedTimestampPairsInFlight() const {
        return 3;
    }
    virtual void writeTimestamp(GpuTimestampHandle, TimestampSlot) {}
    virtual bool readTimestampPairMs(GpuTimestampHandle, float &) {
        return false;
    }
};

RenderDevice *device();
void setDevice(RenderDevice *renderDevice);

} // namespace IRRender

#endif /* RENDER_DEVICE_H */
