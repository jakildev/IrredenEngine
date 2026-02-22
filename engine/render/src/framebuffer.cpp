// OpenGL Framebuffer with extra pixel buffer for scaled up
// pixel pixel-art rendering with smooth scroll.

#include <irreden/ir_profile.hpp>

#include <irreden/render/framebuffer.hpp>
#include <irreden/render/ir_gl_api.hpp>
#include <irreden/render/rendering_rm.hpp>

namespace IRRender {

Framebuffer::Framebuffer(
    ivec2 resolution, ivec2 extraPixelBuffer, GLenum formatColor, GLenum formatDepthStencil
)
    : m_resolution(resolution)
    , m_extraPixelBuffer(extraPixelBuffer)
    , m_resolutionPlusBuffer(m_resolution + m_extraPixelBuffer)
    , m_textureColor{GL_TEXTURE_2D, (GLuint)m_resolutionPlusBuffer.x, (GLuint)m_resolutionPlusBuffer.y, formatColor, GL_REPEAT, GL_NEAREST}
    , m_textureDepth{
          GL_TEXTURE_2D,
          (GLuint)m_resolutionPlusBuffer.x,
          (GLuint)m_resolutionPlusBuffer.y,
          formatDepthStencil
      } {
    ENG_API->glCreateFramebuffers(1, &m_id);
    ENG_API->glNamedFramebufferTexture(m_id, GL_COLOR_ATTACHMENT0, m_textureColor.getHandle(), 0);
    ENG_API->glNamedFramebufferTexture(
        m_id,
        GL_DEPTH_STENCIL_ATTACHMENT,
        m_textureDepth.getHandle(),
        0
    );
    checkSuccess();

    IRE_LOG_INFO(
        "Created framebuffer with id={}, resolution={},{}, extraPixelBuffer={},{}",
        m_id,
        m_resolution.x,
        m_resolution.y,
        m_extraPixelBuffer.x,
        m_extraPixelBuffer.y
    );
}

Framebuffer::~Framebuffer() {
    ENG_API->glDeleteFramebuffers(1, &m_id);
}

// Binds for both reading and writing, but maybe should only be for writing
void Framebuffer::bind() const {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
    ENG_API->glBindFramebuffer(GL_FRAMEBUFFER, m_id);
    ENG_API->glViewport(0, 0, m_resolutionPlusBuffer.x, m_resolutionPlusBuffer.y);
    ENG_API->glEnable(GL_DEPTH_TEST);
    ENG_API->glDepthFunc(GL_LESS);
    // ENG_API->glDisable(GL_DEPTH_TEST);
    // ENG_API->glEnable(GL_BLEND);
}

void Framebuffer::unbind() {
    ENG_API->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::clear() const {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
    ENG_API->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    ENG_API->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Framebuffer::checkSuccess() {
    GLenum status = ENG_API->glCheckNamedFramebufferStatus(m_id, GL_FRAMEBUFFER);

    IR_ASSERT(status == GL_FRAMEBUFFER_COMPLETE, "Attempted to create incomplete framebuffer");
}

} // namespace IRRender