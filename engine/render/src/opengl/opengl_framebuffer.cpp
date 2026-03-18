#include <irreden/ir_profile.hpp>

#include <irreden/render/framebuffer.hpp>
#include <irreden/render/ir_gl_api.hpp>

namespace IRRender {

class OpenGLFramebufferImpl final : public FramebufferImpl {
  public:
    OpenGLFramebufferImpl(const Texture2D &textureColor, const Texture2D &textureDepth) {
        const uvec2 size = textureColor.getSize();
        m_width = static_cast<GLsizei>(size.x);
        m_height = static_cast<GLsizei>(size.y);
        ENG_API->glCreateFramebuffers(1, &m_id);
        ENG_API->glNamedFramebufferTexture(m_id, GL_COLOR_ATTACHMENT0, textureColor.getHandle(), 0);
        ENG_API->glNamedFramebufferTexture(
            m_id, GL_DEPTH_STENCIL_ATTACHMENT, textureDepth.getHandle(), 0
        );
        GLenum status = ENG_API->glCheckNamedFramebufferStatus(m_id, GL_FRAMEBUFFER);
        IR_ASSERT(status == GL_FRAMEBUFFER_COMPLETE, "Attempted to create incomplete framebuffer");
    }

    ~OpenGLFramebufferImpl() override {
        ENG_API->glDeleteFramebuffers(1, &m_id);
    }

    void bind() const override {
        ENG_API->glBindFramebuffer(GL_FRAMEBUFFER, m_id);
        ENG_API->glViewport(0, 0, m_width, m_height);
        ENG_API->glEnable(GL_DEPTH_TEST);
        ENG_API->glDepthFunc(GL_LESS);
    }

    void unbind() override {
        ENG_API->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void clear() const override {
        ENG_API->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        ENG_API->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

  private:
    GLuint m_id = 0;
    GLsizei m_width = 0;
    GLsizei m_height = 0;
};

std::unique_ptr<FramebufferImpl> createFramebufferImpl(
    const Texture2D &textureColor,
    const Texture2D &textureDepth
) {
    return std::make_unique<OpenGLFramebufferImpl>(textureColor, textureDepth);
}

} // namespace IRRender
