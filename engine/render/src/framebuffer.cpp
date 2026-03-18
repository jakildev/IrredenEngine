#include <irreden/render/framebuffer.hpp>

#include <utility>

namespace IRRender {

std::unique_ptr<FramebufferImpl> createFramebufferImpl(
    const Texture2D &textureColor,
    const Texture2D &textureDepth
);

Framebuffer::Framebuffer(
    ivec2 resolution, ivec2 extraPixelBuffer, TextureFormat formatColor, TextureFormat formatDepthStencil
)
    : m_resolution(resolution)
    , m_extraPixelBuffer(extraPixelBuffer)
    , m_resolutionPlusBuffer(m_resolution + m_extraPixelBuffer)
    , m_textureColor{
          TextureKind::TEXTURE_2D,
          static_cast<unsigned int>(m_resolutionPlusBuffer.x),
          static_cast<unsigned int>(m_resolutionPlusBuffer.y),
          formatColor,
          TextureWrap::REPEAT,
          TextureFilter::NEAREST
      }
    , m_textureDepth{
          TextureKind::TEXTURE_2D,
          static_cast<unsigned int>(m_resolutionPlusBuffer.x),
          static_cast<unsigned int>(m_resolutionPlusBuffer.y),
          formatDepthStencil
      }
    , m_impl(createFramebufferImpl(m_textureColor, m_textureDepth)) {}

Framebuffer::~Framebuffer() = default;

void Framebuffer::bind() const {
    m_impl->bind();
}

void Framebuffer::unbind() {
    m_impl->unbind();
}

void Framebuffer::clear() const {
    m_impl->clear();
}

} // namespace IRRender