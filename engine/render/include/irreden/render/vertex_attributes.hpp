#ifndef VERTEX_ATTRIBUTES_H
#define VERTEX_ATTRIBUTES_H

#include <irreden/render/ir_render_enums.hpp>

namespace IRRender {
struct VertexArrayAttribute {
    int size_;
    VertexAttributeDataType type_;
    bool normalized_;
};
const VertexArrayAttribute kAttrFloat2 = {2, VertexAttributeDataType::FLOAT32, false};
const VertexArrayAttribute kAttrFloat3 = {3, VertexAttributeDataType::FLOAT32, false};
const VertexArrayAttribute kAttrFloat4 = {4, VertexAttributeDataType::FLOAT32, false};
const VertexArrayAttribute kAttrList2Float2[2] = {kAttrFloat2, kAttrFloat2};
const VertexArrayAttribute kAttrList2Float3[2] = {kAttrFloat3, kAttrFloat3};
const VertexArrayAttribute kAttrListDebugVertex[2] = {kAttrFloat2, kAttrFloat4};

} // namespace IRRender

#endif /* VERTEX_ATTRIBUTES_H */
