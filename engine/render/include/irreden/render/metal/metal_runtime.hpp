#pragma once

#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/metal/metal_types.hpp>

#include <array>
#include <cstdint>

namespace IRRender {

class RenderDevice;

// Headless render-device bring-up for GPU unit tests (vehicle A, #1640).
// Creates a windowless Metal device, initializes the runtime with a null layer,
// wires IRRender::device(), and opens an initial command buffer — everything a
// compute test needs to drive dispatchCompute + readback without a swapchain.
// Returns nullptr when no Metal device is available so the caller can skip.
RenderDevice *bootstrapHeadlessRenderDevice();

class MetalPipelineStateProvider {
  public:
    virtual ~MetalPipelineStateProvider() = default;
    virtual bool isComputePipeline() const = 0;
    virtual MTL::Size getThreadsPerThreadgroup() const = 0;
    // True only for compute kernels registered in metal_pipeline.cpp's
    // functionUsesImageAtomicScratch — the kernels that declare the R32I
    // image-atomic scratch at kMetalImageAtomicScratchSlot. Gates the
    // scratch bind in bindComputeResources so the slot stays free for
    // kernels that declare an unrelated buffer there (#1619: the sticky
    // scratch clobbered c_revoxelize_detached's params UBO at slot 16).
    virtual bool usesImageAtomicScratch() const = 0;
    virtual MTL::RenderPipelineState *getRenderPipelineState(
        MTL::PixelFormat colorPixelFormat,
        MTL::PixelFormat depthPixelFormat,
        const MTL::VertexDescriptor *vertexDescriptor
    ) = 0;
    virtual MTL::ComputePipelineState *getComputePipelineState() = 0;
};

struct MetalBufferBinding {
    MTL::Buffer *buffer_ = nullptr;
    NS::UInteger offset_ = 0;
};

struct MetalVertexLayoutBinding {
    MTL::Buffer *vertexBuffer_ = nullptr;
    MTL::Buffer *indexBuffer_ = nullptr;
    MTL::VertexDescriptor *vertexDescriptor_ = nullptr;
};

void initializeMetalRuntime(MTL::Device *device, CA::MetalLayer *layer);
void setMetalBootstrapDevice(MTL::Device *device);
void shutdownMetalRuntime();
void resizeMetalDrawable(int width, int height);

MTL::Device *metalDevice();
CA::MetalLayer *metalLayer();
MTL::CommandQueue *metalCommandQueue();
MTL::CommandBuffer *metalCommandBuffer();
CA::MetalDrawable *metalDrawable();
MTL::DepthStencilState *currentMetalDepthStencilState();

void setMetalCommandBuffer(MTL::CommandBuffer *commandBuffer);
void setMetalDrawable(CA::MetalDrawable *drawable);
void setMetalDepthTestEnabled(bool enabled);
void setMetalDepthWriteEnabled(bool enabled);

void setActiveMetalPipeline(MetalPipelineStateProvider *pipeline);
MetalPipelineStateProvider *activeMetalPipeline();

void setActiveMetalVertexLayout(
    MTL::Buffer *vertexBuffer,
    MTL::Buffer *indexBuffer,
    MTL::VertexDescriptor *vertexDescriptor
);
const MetalVertexLayoutBinding &activeMetalVertexLayout();

void bindMetalBuffer(BufferTarget target, std::uint32_t index, MTL::Buffer *buffer, NS::UInteger offset);
const MetalBufferBinding &boundMetalBuffer(BufferTarget target, std::uint32_t index);

void bindMetalTexture(std::uint32_t unit, MTL::Texture *texture);
MTL::Texture *boundMetalTexture(std::uint32_t unit);

void bindMetalImageTexture(std::uint32_t unit, MTL::Texture *texture);
MTL::Texture *boundMetalImageTexture(std::uint32_t unit);

// Null every sticky sampler/image bind slot that still points at @p texture.
// Bindings are sticky (set on bind, never cleared), so a destroyed texture
// otherwise lingers as a dangling pointer that bindRenderResources /
// bindComputeResources re-binds on a later dispatch — setTexture on the freed
// handle EXC_BAD_ACCESSes (#1961). Call from the Metal texture destructor
// before releasing the handle.
void untrackMetalTexture(MTL::Texture *texture);

void bindMetalDefaultRenderTarget();
void bindMetalFramebufferRenderTarget(
    MTL::Texture *colorTexture,
    MTL::Texture *depthTexture,
    MTL::PixelFormat colorPixelFormat,
    MTL::PixelFormat depthPixelFormat
);
void requestMetalRenderTargetClear();
bool consumeMetalRenderTargetClear();
bool metalUsesDefaultRenderTarget();
MTL::Texture *metalCurrentColorTexture();
MTL::Texture *metalCurrentDepthTexture();
MTL::PixelFormat metalCurrentColorPixelFormat();
MTL::PixelFormat metalCurrentDepthPixelFormat();

// Buffer orphaning: subData() orphans an MTL::Buffer (allocates a fresh
// one + defers release of the old) only when the buffer has been encoded
// into a command encoder since the previous wait point. The
// encoded-since-last-wait set drives that decision — the set is populated
// at every encoder bind site (see metal_render_impl.cpp's
// bindRenderResources / bindComputeResources / dispatch + draw paths) and
// cleared inside releaseDeferredMetalBuffers() after the GPU has finished
// consuming the prior frame's encoders. Until a buffer enters the set in
// the current frame, subData() writes in place — no alloc, no full-buffer
// copy. See metal_runtime.cpp for the lifetime contract.
void deferReleaseMetalBuffer(MTL::Buffer *buffer);
void releaseDeferredMetalBuffers();
void replaceMetalBufferInBindings(MTL::Buffer *oldBuffer, MTL::Buffer *newBuffer);

// Null every sticky uniform/storage bind slot (and the active vertex-layout
// slots) that still points at @p buffer, and drop it from the encoded-set.
// The buffer twin of untrackMetalTexture: bindings are sticky, so a destroyed
// buffer otherwise lingers as a dangling pointer that bindRenderResources /
// bindComputeResources re-binds on a later dispatch — setBuffer objc_retains
// the freed handle and EXC_BAD_ACCESSes. Realized by a rotation-lifecycle
// buffer bound at a slot no cardinal-path pass re-binds (#2412: the #2334
// overflow relight's slot-8 bind survived the per-axis release and crashed
// STAGE_1's next dispatch at the yaw→0 transition). GL needs no equivalent —
// deleting a GL buffer detaches it from every binding point. Call from the
// Metal buffer destructor before releasing the handle.
void untrackMetalBuffer(MTL::Buffer *buffer);

void markMetalBufferEncoded(MTL::Buffer *buffer);
bool wasMetalBufferEncoded(MTL::Buffer *buffer);

// Per-texture scratch buffer mirroring an R32I distance texture, used as
// the target for `device atomic_int*` min ops because MSL has no portable
// image-atomic syntax across macOS versions. See metal_runtime.cpp.
//
// Slot 16 deliberately ALIASES kBufferIndex_RevoxelizeDetachedParams: the
// Metal 0-30 buffer table has no free index, and on Metal UNIFORM and
// SHADER_STORAGE share one index space (unlike GL's separate bind-point
// spaces). The alias is safe ONLY because the scratch is bound per-kernel:
// bindComputeResources consults
// MetalPipelineStateProvider::usesImageAtomicScratch(), resolved from the
// explicit kernel list in metal_pipeline.cpp. A new kernel that consumes
// the scratch MUST be added to functionUsesImageAtomicScratch there —
// like the threadgroupSizeForFunctionName map, this does not self-detect.
constexpr std::uint32_t kMetalImageAtomicScratchSlot = 16;
MTL::Buffer *ensureImageAtomicScratchBuffer(MTL::Texture *texture);
MTL::Buffer *lookupImageAtomicScratchBuffer(MTL::Texture *texture);
void releaseImageAtomicScratchBuffer(MTL::Texture *texture);
void setCurrentImageAtomicScratch(MTL::Buffer *buffer);
MTL::Buffer *currentImageAtomicScratch();

// Called from MetalTexture2DImpl's destructor; prevents a recycled texture
// address from aliasing a stale entry in the clear-source-buffer map.
void removeClearSourceBuffer(MTL::Texture *texture);

} // namespace IRRender
