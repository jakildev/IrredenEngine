#include <irreden/ir_profile.hpp>

#include <irreden/render/buffer.hpp>
#include <irreden/render/ir_gl_api.hpp>
#include <irreden/render/opengl/opengl_types.hpp>

namespace IRRender {

class OpenGLBufferImpl final : public BufferImpl {
  public:
    OpenGLBufferImpl(const void *data, std::size_t size, std::uint32_t flags) {
        ENG_API->glCreateBuffers(1, &m_handle);
        ENG_API->glNamedBufferStorage(m_handle, static_cast<GLsizeiptr>(size), data, toGLBufferStorageFlags(flags));
        IRE_LOG_INFO("Created GL buffer: {}", m_handle);
    }

    OpenGLBufferImpl(
        const void *data,
        std::size_t size,
        std::uint32_t flags,
        BufferTarget target,
        std::uint32_t index
    )
        : OpenGLBufferImpl(data, size, flags) {
        bindBase(target, index);
    }

    ~OpenGLBufferImpl() override {
        IRE_LOG_DEBUG("Deleting GL buffer handle={}", m_handle);
        ENG_API->glDeleteBuffers(1, &m_handle);
    }

    std::uint32_t getHandle() const override {
        return m_handle;
    }

    void subData(std::ptrdiff_t offset, std::size_t size, const void *data) const override {
        ENG_API->glNamedBufferSubData(
            m_handle, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data
        );
    }

    void getSubData(std::ptrdiff_t offset, std::size_t size, void *data) const override {
        ENG_API->glGetNamedBufferSubData(
            m_handle, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data
        );
    }

    void bindRange(
        BufferTarget target, std::uint32_t index, std::ptrdiff_t offset, std::size_t size
    ) override {
        ENG_API->glBindBufferRange(
            toGLBufferTarget(target),
            index,
            m_handle,
            static_cast<GLintptr>(offset),
            static_cast<GLsizeiptr>(size)
        );
    }

    void bindBase(BufferTarget target, std::uint32_t index) override {
        ENG_API->glBindBufferBase(toGLBufferTarget(target), index, m_handle);
    }

  private:
    GLuint m_handle = 0;
};

std::unique_ptr<BufferImpl> createBufferImpl(const void *data, std::size_t size, std::uint32_t flags) {
    return std::make_unique<OpenGLBufferImpl>(data, size, flags);
}

std::unique_ptr<BufferImpl> createBufferImpl(
    const void *data,
    std::size_t size,
    std::uint32_t flags,
    BufferTarget target,
    std::uint32_t index
) {
    return std::make_unique<OpenGLBufferImpl>(data, size, flags, target, index);
}

} // namespace IRRender
