#pragma once

#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/metal/metal_types.hpp>

#include <array>
#include <cstdint>

namespace IRRender {

class MetalPipelineStateProvider {
  public:
    virtual ~MetalPipelineStateProvider() = default;
    virtual bool isComputePipeline() const = 0;
    virtual MTL::Size getThreadsPerThreadgroup() const = 0;
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

void markMetalBufferEncoded(MTL::Buffer *buffer);
bool wasMetalBufferEncoded(MTL::Buffer *buffer);

// Per-texture scratch buffer mirroring an R32I distance texture, used as
// the target for `device atomic_int*` min ops because MSL has no portable
// image-atomic syntax across macOS versions. See metal_runtime.cpp.
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
