#ifndef RENDER_DEVICE_H
#define RENDER_DEVICE_H

#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/gpu_frame_timing.hpp>

#include <cstddef>
#include <cstdint>

namespace IRRender {

class Buffer;
class Texture2D;

struct GpuMemoryStats {
    std::size_t processAllocatedBytes_ = 0;
    std::size_t deviceCapacityBytes_ = 0;
    std::size_t deviceAvailableBytes_ = 0;
};

using GpuTimestampHandle = std::uint32_t;
constexpr GpuTimestampHandle kInvalidGpuTimestampHandle = 0;

enum class TimestampSlot {
    START,
    END,
};

enum class TimestampReadStatus {
    PENDING,
    READY,
    INVALID,
};

class RenderDevice {
  public:
    virtual ~RenderDevice() = default;

    virtual void setGpuFrameTimingEnabled(bool enabled) {
        m_gpuFrameTiming.reset(enabled, false);
    }
    const GpuFrameTimingStats &gpuFrameTimingStats() const {
        return m_gpuFrameTiming.stats();
    }
    virtual GpuMemoryStats gpuMemoryStats() const {
        return {};
    }

    virtual void beginFrame() = 0;
    virtual void present() = 0;

    virtual void dispatchCompute(std::uint32_t x, std::uint32_t y, std::uint32_t z) = 0;
    virtual void dispatchComputeIndirect(const Buffer *indirectBuffer, std::ptrdiff_t offset) = 0;
    virtual void memoryBarrier(BarrierType barrierType) = 0;
    // Clear one indexed UNIFORM or SHADER_STORAGE binding without disturbing
    // the other binding table at the same index.
    virtual void unbindBufferBase(BufferTarget target, std::uint32_t index) = 0;
    virtual void drawElements(DrawMode drawMode, int count, IndexType indexType) = 0;
    virtual void
    drawElementsInstanced(DrawMode drawMode, int count, IndexType indexType, int instanceCount) = 0;
    // Indirect instanced indexed draw: index/instance counts come from a
    // DrawElementsIndirectCommand (GL) / MTLDrawIndexedPrimitivesIndirectArguments
    // (Metal) at @p indirectOffset in @p indirectBuffer, GPU-written by a compute
    // pass earlier in the same frame. The bound element buffer supplies the
    // indices; the contract is symmetric with dispatchComputeIndirect.
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
    // GPU-to-GPU copy of @p source's whole level-0 extent into @p destination,
    // ordered with the surrounding commands, with no CPU wait and no format
    // conversion. Both must be 2D textures of identical size and native pixel
    // format.
    virtual void copyTexture2D(const Texture2D &source, Texture2D &destination) = 0;
    virtual void setPolygonMode(PolygonMode polygonMode) = 0;
    virtual void bindDefaultFramebuffer() = 0;
    virtual void clearDefaultFramebuffer() = 0;
    virtual bool readDefaultFramebuffer(int x, int y, int width, int height, void *rgbaData) = 0;
    virtual void enableBlending() = 0;
    virtual void disableBlending() = 0;
    virtual void setDepthTest(bool enabled) = 0;
    virtual void setDepthWrite(bool enabled) = 0;
    virtual void clearTexImage(const Texture2D *texture, int level, const void *data) = 0;
    // GPU-side repeating-single-byte fill of @p sizeBytes of a buffer from
    // @p offsetBytes — the semantics Metal's blit fillBuffer offers natively,
    // matched on GL by glClearNamedBufferSubData with an R8UI pattern. Suited
    // to sentinels whose every byte is identical (0x00, 0xFF); a multi-byte
    // pattern like 0x0000FFFF needs a clear dispatch instead (see the
    // GPU-buffer-sentinel gotcha in engine/prefabs/CLAUDE.md).
    virtual void fillBuffer(
        const Buffer *buffer, std::size_t offsetBytes, std::size_t sizeBytes, std::uint8_t byteValue
    ) = 0;
    // The fill of a buffer's first @p sizeBytes.
    void fillBuffer(const Buffer *buffer, std::size_t sizeBytes, std::uint8_t byteValue) {
        fillBuffer(buffer, 0, sizeBytes, byteValue);
    }
    // GPU-timeline copy of @p sizeBytes from @p source into @p destination:
    // glCopyNamedBufferSubData on GL, a blit on Metal. A transfer, not a
    // shader write, so the destination is never tracked as shader-written:
    // NVIDIA GL reads indirect arguments from such a buffer without draining
    // the queue, and unlike subData into a GPU-written buffer the copy never
    // blocks the CPU. Shader writes feeding the copy need a BUFFER_UPDATE
    // barrier first.
    virtual void copyBufferSubData(
        const Buffer *source,
        std::ptrdiff_t sourceOffset,
        const Buffer *destination,
        std::ptrdiff_t destinationOffset,
        std::size_t sizeBytes
    ) = 0;
    virtual void finish() = 0;
    // GPU completion without a CPU wait. Work encoded now belongs to
    // submissionSerial(); it has finished on the GPU once
    // completedSubmissionSerial() reaches that value, so a CPU read of what
    // it wrote returns the final bytes and never stalls. Metal's getSubData
    // is a plain read of shared storage with no implicit sync, so a readback
    // of GPU-written data must gate on this rather than on call order.
    // Polling never blocks; both serials only increase.
    virtual std::uint64_t submissionSerial() const = 0;
    virtual std::uint64_t completedSubmissionSerial() = 0;

    // Make @p texture's **level 0** contents reflect its image-atomic state. A
    // no-op on backends whose image atomics write the texture directly (OpenGL's
    // imageAtomicMin), a scratch-buffer -> texture materialization on Metal,
    // whose atomics land in a sibling buffer instead. Call it wherever a later
    // pass reads the texture through a plain sampler / access::read and must see depth an
    // atomic pass wrote — the backends agree on the texture only after this.
    // Idempotent and value-safe: texels the atomics never touched resolve
    // their own clear value back onto themselves.
    //
    // Level 0 only, unlike clearTexImage's explicit @p level: the scratch is
    // allocated per texture at its base dimensions (ensureImageAtomicScratchBuffer),
    // so there is no per-level state to resolve. Every image-atomic texture in the
    // engine is single-mip; a mipped one would need the scratch keyed by level
    // first, not just a level argument here.
    virtual void resolveImageAtomicScratch(const Texture2D *) {}

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
    // INVALID completes a sample without a duration; callers must release its
    // ring slot without adding a zero to their timing statistics.
    virtual TimestampReadStatus pollTimestampPairMs(GpuTimestampHandle handle, float &outMs) {
        return readTimestampPairMs(handle, outMs) ? TimestampReadStatus::READY
                                                  : TimestampReadStatus::PENDING;
    }

  protected:
    GpuFrameTimingAccumulator m_gpuFrameTiming;
};

RenderDevice *device();
void setDevice(RenderDevice *renderDevice);

// Overwrite @p bytes of @p target at @p targetOffset from @p seed on the GPU
// timeline, ordered after every queued shader write to @p target. A CPU
// subData there would block until those writes retired.
inline void resetBufferFromSeed(
    const Buffer *seed, const Buffer *target, std::ptrdiff_t targetOffset, std::size_t bytes
) {
    device()->memoryBarrier(BarrierType::BUFFER_UPDATE);
    device()->copyBufferSubData(seed, 0, target, targetOffset, bytes);
}

// Copy shader-written indirect arguments into @p args, a buffer no program
// binds, so the indirect dispatches and draws that follow read them without
// the CPU draining the queue on NVIDIA GL.
inline void publishIndirectArgs(
    const Buffer *source,
    std::ptrdiff_t sourceOffset,
    const Buffer *args,
    std::ptrdiff_t argsOffset,
    std::size_t bytes
) {
    device()->memoryBarrier(BarrierType::BUFFER_UPDATE);
    device()->copyBufferSubData(source, sourceOffset, args, argsOffset, bytes);
    device()->memoryBarrier(BarrierType::COMMAND);
}

} // namespace IRRender

#endif /* RENDER_DEVICE_H */
