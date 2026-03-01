#include <irreden/ir_profile.hpp>

#include <irreden/render/buffer.hpp>
#include <irreden/render/ir_gl_api.hpp>

namespace IRRender {

Buffer::Buffer(const void *data, GLsizeiptr size, GLbitfield flags) {
    ENG_API->glCreateBuffers(1, &m_handle);
    ENG_API->glNamedBufferStorage(m_handle, size, data, flags);
    IRE_LOG_INFO("Created GL buffer: {}", m_handle);
}

Buffer::Buffer(const void *data, GLsizeiptr size, GLbitfield flags, GLenum target, GLuint index)
    : Buffer(data, size, flags) {
    bindBase(target, index);
}

Buffer::~Buffer() {
    IRE_LOG_DEBUG("Deleting GL buffer handle={}", m_handle);
    ENG_API->glDeleteBuffers(1, &m_handle);
}

GLuint Buffer::getHandle() {
    return m_handle;
}

void Buffer::subData(GLintptr offset, GLsizeiptr size, const void *data) const {
    ENG_API->glNamedBufferSubData(m_handle, offset, size, data);
}

void Buffer::getSubData(GLintptr offset, GLsizeiptr size, void *data) const {
    ENG_API->glGetNamedBufferSubData(m_handle, offset, size, data);
}

void Buffer::bindRange(GLenum target, GLuint index, GLintptr offset, GLsizeiptr size) {
    ENG_API->glBindBufferRange(target, index, m_handle, offset, size);
}

void Buffer::bindBase(GLenum target, GLuint index) {
    ENG_API->glBindBufferBase(target, index, m_handle);
}

} // namespace IRRender