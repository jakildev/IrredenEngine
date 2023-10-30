/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\buffer.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef BUFFER_H
#define BUFFER_H

#include <glad/glad.h>

namespace IRRender {

    class Buffer {
    public:
        Buffer(
            const void* data,
            GLsizeiptr size,
            GLbitfield flags
        );
        Buffer(
            const void* data,
            GLsizeiptr size,
            GLbitfield flags,
            GLenum target,
            GLuint index
        );
        ~Buffer();

        GLuint getHandle();
        void subData(
            GLintptr offset,
            GLsizeiptr size,
            const void *data
        );
        void bindRange(
            GLenum target,
            GLuint index,
            GLintptr offset,
            GLsizeiptr size
        );
        void bindBase(
            GLenum target,
            GLuint index
        );
    private:
        GLuint m_handle;
    };

} // namespace IRRender

#endif /* BUFFER_H */
