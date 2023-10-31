/*
 * Project: Irreden Engine
 * File: vertex_attributes.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef VERTEX_ATTRIBUTES_H
#define VERTEX_ATTRIBUTES_H

#include <glad/glad.h>

namespace IRRender {
    struct VertexArrayAttribute {
        GLint size_;
        GLenum type_;
        GLboolean normalized_;
    };
    const VertexArrayAttribute kAttrFloat2 = {
        2,
        GL_FLOAT,
        GL_FALSE
    };
    const VertexArrayAttribute kAttrFloat3 = {
        3,
        GL_FLOAT,
        GL_FALSE
    };
    const VertexArrayAttribute kAttrList2Float2[2] = {
        kAttrFloat2,
        kAttrFloat2
    };
    const VertexArrayAttribute kAttrList2Float3[2] = {
        kAttrFloat3,
        kAttrFloat3
    };

} // namespace IRRender

#endif /* VERTEX_ATTRIBUTES_H */
