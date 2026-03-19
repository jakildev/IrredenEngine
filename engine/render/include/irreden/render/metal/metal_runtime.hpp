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

std::uint32_t registerMetalBufferHandle(MTL::Buffer *buffer);
void unregisterMetalBufferHandle(std::uint32_t handle);
MTL::Buffer *lookupMetalBufferHandle(std::uint32_t handle);

} // namespace IRRender
