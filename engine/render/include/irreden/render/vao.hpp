#ifndef VAO_H
#define VAO_H

#include <irreden/render/opengl/opengl_types.hpp>
#include <irreden/render/ir_gl_api.hpp>
#include <irreden/render/vertex_attributes.hpp>

namespace IRRender {

const unsigned int kMaxVertexAttributes = 16;

class VAO {
  public:
    VAO(GLuint vertexBufferHandle, GLuint indexBufferHandle, unsigned int numAttributes,
        const VertexArrayAttribute *attributes);
    ~VAO();

    void bind() const;

  private:
    GLuint m_handle;
    GLsizei m_stride;

    void initVertexBufferAttributes(unsigned int numAttributes,
                                    const VertexArrayAttribute *attributes,
                                    const size_t *attributeSizes, GLuint bindingIndex);
};

} // namespace IRRender

#endif /* VAO_H */
