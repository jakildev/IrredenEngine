/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\vao.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/render/vao.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>

namespace IRRender {

    VAO::VAO(
        GLuint vertexBufferHandle,
        GLuint indexBufferHandle,
        unsigned int numAttributes,
        const VertexArrayAttribute* attributes
    )
    :   m_handle(0)
    ,   m_stride(0)
    {
        ENG_API->glCreateVertexArrays(1, &m_handle);
        GLuint bindingIndex = 0; // TODO: change to param if need more than one
        IR_ENG_ASSERT(
            numAttributes <= kMaxVertexAttributes,
            "Too many vertex attributes for VAO"
        );

        // calc stride
        size_t attributeSizes[numAttributes];
        for (int i = 0; i < numAttributes; i++) {
            attributeSizes[i] =
                attributes[i].size_ *
                kMapSizeofGLType.at(attributes[i].type_)
            ;
            m_stride += attributeSizes[i];
        }

        if(indexBufferHandle) {
            ENG_API->glVertexArrayElementBuffer(
                m_handle,
                indexBufferHandle
            );
        }
        ENG_API->glVertexArrayVertexBuffer(
            m_handle,
            bindingIndex,
            vertexBufferHandle,
            0,
            m_stride
        );
        initVertexBufferAttributes(
            numAttributes,
            attributes,
            attributeSizes,
            bindingIndex
        );
        IRProfile::engLogInfo(
            "Created VAO: {}",
            m_handle
        );
    }

    VAO::~VAO() {
        ENG_API->glDeleteVertexArrays(1, &m_handle);
        IRProfile::engLogInfo("Deleted VAO: {}", m_handle);
    }

    void VAO::bind() const {
        ENG_API->glBindVertexArray(m_handle);
    }

    void VAO::initVertexBufferAttributes(
        unsigned int numAttributes,
        const VertexArrayAttribute* attributes,
        const size_t* attributeSizes,
        GLuint bindingIndex)
    {
        GLuint offset = 0;
        for(int i = 0; i < numAttributes; i++) {
            ENG_API->glEnableVertexArrayAttrib(m_handle, i);
            ENG_API->glVertexArrayAttribFormat(
                m_handle,
                i,
                attributes[i].size_,
                attributes[i].type_,
                attributes[i].normalized_,
                offset
            );
            ENG_API->glVertexArrayAttribBinding(
                m_handle,
                i,
                bindingIndex
            );
            offset += attributeSizes[i];
        }
    }

} // namespace IRRender
