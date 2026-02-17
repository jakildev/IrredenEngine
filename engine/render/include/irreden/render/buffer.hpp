#ifndef BUFFER_H
#define BUFFER_H

#include <irreden/render/opengl/opengl_types.hpp>

namespace IRRender {

class Buffer {
  public:
    Buffer(const void *data, GLsizeiptr size, GLbitfield flags);
    Buffer(const void *data, GLsizeiptr size, GLbitfield flags, GLenum target, GLuint index);
    ~Buffer();

    GLuint getHandle();
    void subData(GLintptr offset, GLsizeiptr size, const void *data) const;
    void bindRange(GLenum target, GLuint index, GLintptr offset, GLsizeiptr size);
    void bindBase(GLenum target, GLuint index);

  private:
    GLuint m_handle;
};

} // namespace IRRender

#endif /* BUFFER_H */
