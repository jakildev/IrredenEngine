#include <irreden/ir_profile.hpp>

#include <irreden/render/ir_gl_api.hpp>
#include <irreden/render/opengl/opengl_types.hpp>
#include <irreden/render/vao.hpp>

namespace IRRender {

class OpenGLVertexLayoutImpl final : public VertexLayoutImpl {
  public:
    OpenGLVertexLayoutImpl(
        std::uint32_t vertexBufferHandle,
        std::uint32_t indexBufferHandle,
        unsigned int numAttributes,
        const VertexArrayAttribute *attributes
    ) {
        ENG_API->glCreateVertexArrays(1, &m_handle);
        GLuint bindingIndex = 0;
        IR_ASSERT(numAttributes <= kMaxVertexAttributes, "Too many vertex attributes for VAO");

        std::size_t stride = 0;
        std::size_t attributeSizes[kMaxVertexAttributes] = {};
        for (unsigned int i = 0; i < numAttributes; ++i) {
            attributeSizes[i] =
                static_cast<std::size_t>(attributes[i].size_) *
                static_cast<std::size_t>(kMapSizeofGLType.at(toGLVertexAttributeDataType(attributes[i].type_)));
            stride += attributeSizes[i];
        }

        if (indexBufferHandle != 0) {
            ENG_API->glVertexArrayElementBuffer(m_handle, indexBufferHandle);
        }
        ENG_API->glVertexArrayVertexBuffer(
            m_handle, bindingIndex, vertexBufferHandle, 0, static_cast<GLsizei>(stride)
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
    std::uint32_t vertexBufferHandle,
    std::uint32_t indexBufferHandle,
    unsigned int numAttributes,
    const VertexArrayAttribute *attributes
) {
    return std::make_unique<OpenGLVertexLayoutImpl>(
        vertexBufferHandle, indexBufferHandle, numAttributes, attributes
    );
}

} // namespace IRRender
