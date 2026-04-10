#ifndef OPENGL_TYPES_H
#define OPENGL_TYPES_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/shader_names.hpp>

#include <glad/glad.h>

#include <cstdint>
#include <unordered_map>

using namespace IRMath;

namespace IRRender {

const std::unordered_map<GLenum, GLint> kMapSizeofGLType = {
    {GL_BYTE, sizeof(GLbyte)},
    {GL_SHORT, sizeof(GLshort)},
    {GL_INT, sizeof(GLint)},
    {GL_FIXED, sizeof(GLfixed)},
    {GL_FLOAT, sizeof(GLfloat)},
    {GL_HALF_FLOAT, sizeof(GLhalf)},
    {GL_DOUBLE, sizeof(GLdouble)},
    {GL_UNSIGNED_BYTE, sizeof(GLubyte)},
    {GL_UNSIGNED_SHORT, sizeof(GLushort)},
    {GL_UNSIGNED_INT, sizeof(GLuint)},
    {GL_INT_2_10_10_10_REV, sizeof(GLuint)},
    {GL_UNSIGNED_INT_2_10_10_10_REV, sizeof(GLuint)},
    {GL_UNSIGNED_INT_10F_11F_11F_REV, sizeof(GLuint)}
};

const std::unordered_map<GLenum, GLint> kMapUnpackAlignmentofGLType = {
    {GL_R8, 1}, {GL_RGB8, 4}, {GL_RGBA8, 4}, {GL_DEPTH24_STENCIL8, 4}
};

inline GLenum toGLTextureKind(TextureKind textureKind) {
    switch (textureKind) {
        case TextureKind::TEXTURE_2D:
            return GL_TEXTURE_2D;
        case TextureKind::TEXTURE_3D:
            return GL_TEXTURE_3D;
        case TextureKind::TEXTURE_2D_ARRAY:
            return GL_TEXTURE_2D_ARRAY;
    }
    return GL_TEXTURE_2D;
}

inline GLenum toGLTextureAccess(TextureAccess access) {
    switch (access) {
        case TextureAccess::READ_ONLY:
            return GL_READ_ONLY;
        case TextureAccess::WRITE_ONLY:
            return GL_WRITE_ONLY;
        case TextureAccess::READ_WRITE:
            return GL_READ_WRITE;
    }
    return GL_READ_WRITE;
}

inline GLenum toGLTextureFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8:
            return GL_RGBA8;
        case TextureFormat::RGBA32F:
            return GL_RGBA32F;
        case TextureFormat::R32I:
            return GL_R32I;
        case TextureFormat::RG32UI:
            return GL_RG32UI;
        case TextureFormat::DEPTH24_STENCIL8:
            return GL_DEPTH24_STENCIL8;
    }
    return GL_RGBA8;
}

inline GLenum toGLPixelDataFormat(PixelDataFormat format) {
    switch (format) {
        case PixelDataFormat::RGBA:
            return GL_RGBA;
        case PixelDataFormat::RED_INTEGER:
            return GL_RED_INTEGER;
        case PixelDataFormat::RG_INTEGER:
            return GL_RG_INTEGER;
    }
    return GL_RGBA;
}

inline GLenum toGLPixelDataType(PixelDataType type) {
    switch (type) {
        case PixelDataType::UNSIGNED_BYTE:
            return GL_UNSIGNED_BYTE;
        case PixelDataType::INT32:
            return GL_INT;
        case PixelDataType::UINT32:
            return GL_UNSIGNED_INT;
        case PixelDataType::FLOAT32:
            return GL_FLOAT;
    }
    return GL_UNSIGNED_BYTE;
}

inline GLenum toGLTextureWrap(TextureWrap wrap) {
    switch (wrap) {
        case TextureWrap::REPEAT:
            return GL_REPEAT;
        case TextureWrap::CLAMP_TO_EDGE:
            return GL_CLAMP_TO_EDGE;
        case TextureWrap::MIRRORED_REPEAT:
            return GL_MIRRORED_REPEAT;
    }
    return GL_REPEAT;
}

inline GLenum toGLTextureFilter(TextureFilter filter) {
    switch (filter) {
        case TextureFilter::NEAREST:
            return GL_NEAREST;
        case TextureFilter::LINEAR:
            return GL_LINEAR;
    }
    return GL_NEAREST;
}

inline GLenum toGLBufferTarget(BufferTarget target) {
    switch (target) {
        case BufferTarget::VERTEX:
            return GL_ARRAY_BUFFER;
        case BufferTarget::INDEX:
            return GL_ELEMENT_ARRAY_BUFFER;
        case BufferTarget::UNIFORM:
            return GL_UNIFORM_BUFFER;
        case BufferTarget::SHADER_STORAGE:
            return GL_SHADER_STORAGE_BUFFER;
        case BufferTarget::PIXEL_PACK:
            return GL_PIXEL_PACK_BUFFER;
    }
    return GL_ARRAY_BUFFER;
}

inline GLbitfield toGLBufferStorageFlags(std::uint32_t flags) {
    GLbitfield glFlags = 0;
    if ((flags & BUFFER_STORAGE_DYNAMIC) != 0) {
        glFlags |= GL_DYNAMIC_STORAGE_BIT;
    }
    if ((flags & BUFFER_STORAGE_MAP_READ) != 0) {
        glFlags |= GL_MAP_READ_BIT;
    }
    if ((flags & BUFFER_STORAGE_MAP_WRITE) != 0) {
        glFlags |= GL_MAP_WRITE_BIT;
    }
    if ((flags & BUFFER_STORAGE_MAP_PERSISTENT) != 0) {
        glFlags |= GL_MAP_PERSISTENT_BIT;
    }
    if ((flags & BUFFER_STORAGE_MAP_COHERENT) != 0) {
        glFlags |= GL_MAP_COHERENT_BIT;
    }
    return glFlags;
}

inline GLenum toGLShaderType(ShaderType type) {
    switch (type) {
        case ShaderType::VERTEX:
            return GL_VERTEX_SHADER;
        case ShaderType::FRAGMENT:
            return GL_FRAGMENT_SHADER;
        case ShaderType::COMPUTE:
            return GL_COMPUTE_SHADER;
        case ShaderType::GEOMETRY:
            return GL_GEOMETRY_SHADER;
    }
    return GL_VERTEX_SHADER;
}

inline GLbitfield toGLBarrierType(BarrierType barrierType) {
    switch (barrierType) {
        case BarrierType::ALL:
            return GL_ALL_BARRIER_BITS;
        case BarrierType::SHADER_STORAGE:
            return GL_SHADER_STORAGE_BARRIER_BIT;
        case BarrierType::SHADER_IMAGE_ACCESS:
            return GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
        case BarrierType::COMMAND:
            return GL_COMMAND_BARRIER_BIT;
    }
    return GL_ALL_BARRIER_BITS;
}

inline GLenum toGLPolygonMode(PolygonMode polygonMode) {
    switch (polygonMode) {
        case PolygonMode::FILL:
            return GL_FILL;
        case PolygonMode::LINE:
            return GL_LINE;
        case PolygonMode::POINT:
            return GL_POINT;
    }
    return GL_FILL;
}

inline GLenum toGLDrawMode(DrawMode drawMode) {
    switch (drawMode) {
        case DrawMode::TRIANGLES:
            return GL_TRIANGLES;
        case DrawMode::LINES:
            return GL_LINES;
    }
    return GL_TRIANGLES;
}

inline GLenum toGLIndexType(IndexType indexType) {
    switch (indexType) {
        case IndexType::UNSIGNED_SHORT:
            return GL_UNSIGNED_SHORT;
    }
    return GL_UNSIGNED_SHORT;
}

inline GLenum toGLVertexAttributeDataType(VertexAttributeDataType type) {
    switch (type) {
        case VertexAttributeDataType::FLOAT32:
            return GL_FLOAT;
    }
    return GL_FLOAT;
}

} // namespace IRRender

#endif /* OPENGL_TYPES_H */
