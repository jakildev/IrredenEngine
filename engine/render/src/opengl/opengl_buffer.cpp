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

    void *getNativeBuffer() const override {
        return nullptr;
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

    void *mapRange(
        std::ptrdiff_t offset, std::size_t length, std::uint32_t accessFlags
    ) override {
        // Idempotent for the same range: glMapNamedBufferRange returns
        // INVALID_OPERATION on an already-mapped buffer.
        // Cache key omits accessFlags — callers on the same range must use consistent flags.
        if (m_mappedPtr != nullptr && m_mappedOffset == offset && m_mappedLength == length) {
            return m_mappedPtr;
        }
        if (m_mappedPtr != nullptr) {
            ENG_API->glUnmapNamedBuffer(m_handle);
            m_mappedPtr = nullptr;
        }
        m_mappedPtr = ENG_API->glMapNamedBufferRange(
            m_handle,
            static_cast<GLintptr>(offset),
            static_cast<GLsizeiptr>(length),
            toGLBufferStorageFlags(accessFlags)
        );
        m_mappedOffset = offset;
        m_mappedLength = length;
        return m_mappedPtr;
    }

    void unmap() override {
        if (m_mappedPtr == nullptr) {
            return;
        }
        ENG_API->glUnmapNamedBuffer(m_handle);
        m_mappedPtr = nullptr;
    }

  private:
    GLuint m_handle = 0;
    void *m_mappedPtr = nullptr;
    std::ptrdiff_t m_mappedOffset = 0;
    std::size_t m_mappedLength = 0;
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
