#ifndef IR_RENDER_ENUMS_H
#define IR_RENDER_ENUMS_H

#include <cstdint>

namespace IRRender {

enum class TextureKind : std::uint8_t {
    TEXTURE_2D,
    TEXTURE_3D
};

enum class TextureAccess : std::uint8_t {
    READ_ONLY,
    WRITE_ONLY,
    READ_WRITE
};

enum class TextureFormat : std::uint8_t {
    RGBA8,
    RGBA32F,
    R32I,
    RG32UI,
    DEPTH24_STENCIL8
};

enum class PixelDataFormat : std::uint8_t {
    RGBA,
    RED_INTEGER,
    RG_INTEGER
};

enum class PixelDataType : std::uint8_t {
    UNSIGNED_BYTE,
    INT32,
    UINT32,
    FLOAT32
};

enum class TextureWrap : std::uint8_t {
    REPEAT,
    CLAMP_TO_EDGE,
    MIRRORED_REPEAT
};

enum class TextureFilter : std::uint8_t {
    NEAREST,
    LINEAR
};

enum class BufferTarget : std::uint8_t {
    VERTEX,
    INDEX,
    UNIFORM,
    SHADER_STORAGE,
    PIXEL_PACK
};

enum BufferStorageFlag : std::uint32_t {
    BUFFER_STORAGE_NONE = 0,
    BUFFER_STORAGE_DYNAMIC = 1u << 0,
    BUFFER_STORAGE_MAP_READ = 1u << 1,
    BUFFER_STORAGE_MAP_WRITE = 1u << 2,
    BUFFER_STORAGE_MAP_PERSISTENT = 1u << 3,
    BUFFER_STORAGE_MAP_COHERENT = 1u << 4
};

inline constexpr std::uint32_t operator|(BufferStorageFlag lhs, BufferStorageFlag rhs) {
    return static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs);
}

enum class ShaderType : std::uint8_t {
    VERTEX,
    FRAGMENT,
    COMPUTE,
    GEOMETRY
};

enum class BarrierType : std::uint8_t {
    ALL,
    SHADER_STORAGE,
    SHADER_IMAGE_ACCESS,
    COMMAND
};

enum class PolygonMode : std::uint8_t {
    FILL,
    LINE,
    POINT
};

enum class DrawMode : std::uint8_t {
    TRIANGLES,
    LINES
};

enum class IndexType : std::uint8_t {
    UNSIGNED_SHORT
};

enum class VertexAttributeDataType : std::uint8_t {
    FLOAT32
};

} // namespace IRRender

#endif /* IR_RENDER_ENUMS_H */
