/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\buffer.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/render/buffer.hpp>
#include <irreden/ir_profiling.hpp>
#include <irreden/render/ir_gl_api.hpp>

namespace IRRender {

    Buffer::Buffer(
        const void* data,
        GLsizeiptr size,
        GLbitfield flags
    )
    {
        ENG_API->glCreateBuffers(1, &m_handle);
        ENG_API->glNamedBufferStorage(m_handle, size, data, flags);
        IRProfile::engLogInfo("Created GL buffer handle={}", m_handle);
    }

    Buffer::Buffer(
        const void* data,
        GLsizeiptr size,
        GLbitfield flags,
        GLenum target,
        GLuint index
    )
    :   Buffer(data, size, flags)
    {
        bindBase(target, index);
    }

    Buffer::~Buffer()
    {
        ENG_API->glDeleteBuffers(1, &m_handle);
        IRProfile::engLogDebug("Deleted GL buffer handle={}", m_handle);
    }

    GLuint Buffer::getHandle() {
        return m_handle;
    }

    void Buffer::subData(
        GLintptr offset,
        GLsizeiptr size,
        const void *data
    ) {
        ENG_API->glNamedBufferSubData(
            m_handle,
            offset,
            size,
            data
        );
    }

    void Buffer::bindRange(
        GLenum target,
        GLuint index,
        GLintptr offset,
        GLsizeiptr size
    ) {
        ENG_API->glBindBufferRange(
            target,
            index,
            m_handle,
            offset,
            size
        );
    }

    void Buffer::bindBase(
        GLenum target,
        GLuint index
    )
    {
        ENG_API->glBindBufferBase(
            target,
            index,
            m_handle
        );
    }

} // namespace IRRender