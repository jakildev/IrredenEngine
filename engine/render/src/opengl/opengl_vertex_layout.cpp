#include <irreden/ir_profile.hpp>

#include <irreden/render/buffer.hpp>
#include <irreden/render/ir_gl_api.hpp>
#include <irreden/render/opengl/opengl_types.hpp>
#include <irreden/render/vao.hpp>

namespace IRRender {

class OpenGLVertexLayoutImpl final : public VertexLayoutImpl {
  public:
    OpenGLVertexLayoutImpl(
        const Buffer *vertexBuffer,
        const Buffer *indexBuffer,
        unsigned int numAttributes,
        const VertexArrayAttribute *attributes
    ) {
        ENG_API->glCreateVertexArrays(1, &m_handle);
        GLuint bindingIndex = 0;
        IR_ASSERT(numAttributes <= kMaxVertexAttributes, "Too many vertex attributes for VAO");
        IR_ASSERT(vertexBuffer != nullptr, "OpenGLVertexLayoutImpl requires a vertex buffer");

        std::size_t stride = 0;
        std::size_t attributeSizes[kMaxVertexAttributes] = {};
        for (unsigned int i = 0; i < numAttributes; ++i) {
            attributeSizes[i] =
                static_cast<std::size_t>(attributes[i].size_) *
                static_cast<std::size_t>(kMapSizeofGLType.at(toGLVertexAttributeDataType(attributes[i].type_)));
            stride += attributeSizes[i];
        }

        if (indexBuffer != nullptr) {
            ENG_API->glVertexArrayElementBuffer(m_handle, indexBuffer->getHandle());
        }
        ENG_API->glVertexArrayVertexBuffer(
            m_handle, bindingIndex, vertexBuffer->getHandle(), 0, static_cast<GLsizei>(stride)
        );

        GLuint offset = 0;
        for (unsigned int i = 0; i < numAttributes; ++i) {
            ENG_API->glEnableVertexArrayAttrib(m_handle, i);
            ENG_API->glVertexArrayAttribFormat(
                m_handle,
                i,
                attributes[i].size_,
                toGLVertexAttributeDataType(attributes[i].type_),
                attributes[i].normalized_ ? GL_TRUE : GL_FALSE,
                offset
            );
            ENG_API->glVertexArrayAttribBinding(m_handle, i, bindingIndex);
            offset += static_cast<GLuint>(attributeSizes[i]);
        }
    }

    ~OpenGLVertexLayoutImpl() override {
        ENG_API->glDeleteVertexArrays(1, &m_handle);
    }

    void bind() const override {
        ENG_API->glBindVertexArray(m_handle);
    }

  private:
    GLuint m_handle = 0;
};

std::unique_ptr<VertexLayoutImpl> createVertexLayoutImpl(
    const Buffer *vertexBuffer,
    const Buffer *indexBuffer,
    unsigned int numAttributes,
    const VertexArrayAttribute *attributes
) {
    return std::make_unique<OpenGLVertexLayoutImpl>(
        vertexBuffer, indexBuffer, numAttributes, attributes
    );
}

} // namespace IRRender
