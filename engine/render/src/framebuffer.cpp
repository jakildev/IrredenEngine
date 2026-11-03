#include <irreden/render/framebuffer.hpp>
#include <irreden/ir_profile.hpp>

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
    m_impl->bind(nullptr);
}

void Framebuffer::bindWithEntityIds() {
    if (!m_textureEntityIds) {
        m_textureEntityIds = std::make_unique<Texture2D>(
            TextureKind::TEXTURE_2D,
            m_resolutionPlusBuffer.x,
            m_resolutionPlusBuffer.y,
            TextureFormat::RG32UI,
            TextureWrap::CLAMP_TO_EDGE,
            TextureFilter::NEAREST
        );
    }
    m_impl->bind(m_textureEntityIds.get());
}

void Framebuffer::clearEntityIds() {
    const uvec2 zero(0);
    IR_ASSERT(
        m_textureEntityIds != nullptr,
        "Entity-id attachment must be allocated before clearing"
    );
    m_textureEntityIds->clear(PixelDataFormat::RG_INTEGER, PixelDataType::UINT32, &zero);
}

Texture2D &Framebuffer::getTextureEntityIds() {
    IR_ASSERT(
        m_textureEntityIds != nullptr,
        "Entity-id attachment must be allocated before reading"
    );
    return *m_textureEntityIds;
}

void Framebuffer::unbind() {
    m_impl->unbind();
}

void Framebuffer::clear() const {
    m_impl->clear();
}

} // namespace IRRender
