#ifndef IR_RENDER_ENUMS_H
#define IR_RENDER_ENUMS_H

#include <cstdint>

namespace IRRender {

/// @{
/// @name Texture enums

/// Dimensionality of a GPU texture object.
enum class TextureKind : std::uint8_t {
    TEXTURE_2D,       ///< Standard 2-D texture (most canvas textures).
    TEXTURE_3D,       ///< Volume texture (3-D voxel maps).
    TEXTURE_2D_ARRAY  ///< Array of 2-D layers (e.g. sprite sheets).
};

/// Image-unit access qualifier for compute-shader image bindings.
enum class TextureAccess : std::uint8_t {
    READ_ONLY,   ///< @c readonly image (GLSL) / @c texture_access::read (MSL).
    WRITE_ONLY,  ///< @c writeonly image.
    READ_WRITE   ///< @c image (default, both read and write).
};

/// Internal texture format. Matches the GLSL/MSL image format qualifier.
/// - @c R32I — canvas distance texture; written via @c imageAtomicMin.
/// - @c RG32UI — entity-id texture; stores (low, high) 32-bit halves.
/// - @c DEPTH24_STENCIL8 — framebuffer depth+stencil attachment.
enum class TextureFormat : std::uint8_t {
    RGBA8,
    RGBA32F,
    R32I,
    RG32UI,
    DEPTH24_STENCIL8
};

/// CPU-side pixel format for @c glReadPixels / texture upload.
enum class PixelDataFormat : std::uint8_t {
    RGBA,
    RED_INTEGER,
    RG_INTEGER
};

/// CPU-side pixel data type for @c glReadPixels / texture upload.
enum class PixelDataType : std::uint8_t {
    UNSIGNED_BYTE,
    INT32,
    UINT32,
    FLOAT32
};

/// Texture wrap mode for UV coordinates outside [0, 1].
enum class TextureWrap : std::uint8_t {
    REPEAT,
    CLAMP_TO_EDGE,
    MIRRORED_REPEAT
};

/// Texture sampling filter.
enum class TextureFilter : std::uint8_t {
    NEAREST,  ///< Pixel-art / no interpolation.
    LINEAR    ///< Bilinear interpolation.
};

/// @}

/// @{
/// @name Buffer enums

/// GPU buffer binding target.
enum class BufferTarget : std::uint8_t {
    VERTEX,
    INDEX,
    UNIFORM,
    SHADER_STORAGE,
    PIXEL_PACK
};

/// Bit-combinable storage flags for persistent-mapped GPU buffers.
/// Combine with @c |: e.g. @c BUFFER_STORAGE_MAP_READ | @c BUFFER_STORAGE_MAP_PERSISTENT.
/// @note @c MAP_PERSISTENT + @c MAP_COHERENT is used by @c HoveredEntityIdBuffer —
///       read it only after the GPU fence signals or you get last-frame garbage.
enum BufferStorageFlag : std::uint32_t {
    BUFFER_STORAGE_NONE        = 0,
    BUFFER_STORAGE_DYNAMIC     = 1u << 0,
    BUFFER_STORAGE_MAP_READ    = 1u << 1,
    BUFFER_STORAGE_MAP_WRITE   = 1u << 2,
    BUFFER_STORAGE_MAP_PERSISTENT = 1u << 3,
    BUFFER_STORAGE_MAP_COHERENT   = 1u << 4
};

inline constexpr std::uint32_t operator|(BufferStorageFlag lhs, BufferStorageFlag rhs) {
    return static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs);
}

/// @}

/// @{
/// @name Shader / draw enums

/// Stage type passed to @c Shader construction.
enum class ShaderType : std::uint8_t {
    VERTEX,
    FRAGMENT,
    COMPUTE,
    GEOMETRY
};

/// Memory-barrier scope after a compute dispatch. @c ALL is safe but coarse;
/// prefer a narrower scope when possible to avoid unnecessary GPU pipeline flushes.
enum class BarrierType : std::uint8_t {
    ALL,
    SHADER_STORAGE,
    SHADER_IMAGE_ACCESS,
    COMMAND
};

/// Polygon rasterization mode. @c LINE / @c POINT are useful for wireframe debugging.
enum class PolygonMode : std::uint8_t {
    FILL,
    LINE,
    POINT
};

/// Primitive topology for a draw call.
enum class DrawMode : std::uint8_t {
    TRIANGLES,
    LINES
};

/// False-color visualization of lighting buffers, applied during the
/// @c LIGHTING_TO_TRIXEL pass. When set to anything other than @c NONE the
/// final composite color is replaced by the selected debug visualization;
/// upstream lighting passes (AO, sun shadow) still run unchanged so the
/// values being visualized are exactly what the artistic path consumes.
/// - @c NONE        — no overlay; normal artistic lighting/composite.
/// - @c AO          — ambient-occlusion factor as red→green
///                    (red = fully occluded, green = fully unoccluded).
/// - @c LIGHT_LEVEL — combined AO × sun-shadow scalar painted as
///                    blue→white (blue = dark, white = bright).
/// - @c SHADOW      — directional sun-shadow occupancy (black = lit,
///                    magenta = shadowed).
enum class DebugOverlayMode : std::uint8_t {
    NONE = 0,
    AO = 1,
    LIGHT_LEVEL = 2,
    SHADOW = 3
};

/// Index buffer element size. Only @c UNSIGNED_SHORT (uint16) is used.
enum class IndexType : std::uint8_t {
    UNSIGNED_SHORT
};

/// Vertex attribute element type.
enum class VertexAttributeDataType : std::uint8_t {
    FLOAT32
};

/// @}

} // namespace IRRender

#endif /* IR_RENDER_ENUMS_H */
