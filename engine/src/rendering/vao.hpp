/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\vao.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef VAO_H
#define VAO_H

#include <glad/glad.h>
#include "ir_gl_api.hpp"
#include "vertex_attributes.hpp"
#include "ir_rendering.hpp"

namespace IRRendering {

    const unsigned int kMaxVertexAttributes = 16;

    class VAO {
    public:
        VAO(
            GLuint vertexBufferHandle,
            GLuint indexBufferHandle,
            unsigned int numAttributes,
            const VertexArrayAttribute* attributes
        );

        void bind() const;
    private:
        GLuint m_handle;
        GLsizei m_stride;

        void initVertexBufferAttributes(
            unsigned int numAttributes,
            const VertexArrayAttribute* attributes,
            const size_t* attributeSizes,
            GLuint bindingIndex
        );
    };

} // namespace IRRendering

#endif /* VAO_H */
