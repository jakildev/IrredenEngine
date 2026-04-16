#ifndef IR_MATH_TYPES_H
#define IR_MATH_TYPES_H

#define GLM_ENABLE_EXPERIMENTAL

#include <cstdint>

#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <glm/mat2x2.hpp>
#include <glm/vector_relational.hpp>
#include <glm/gtx/color_space.hpp>

namespace IRMath {
/// GLM float vector aliases used throughout the engine.
using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;

/// GLM double-precision vector aliases.
using dvec2 = glm::dvec2;
using dvec3 = glm::dvec3;
using dvec4 = glm::dvec4;

/// GLM signed-integer vector aliases.
using ivec1 = glm::ivec1;
using ivec2 = glm::ivec2;
using ivec3 = glm::ivec3;
using ivec4 = glm::ivec4;

/// GLM unsigned-integer vector aliases.
using uvec2 = glm::uvec2;
using uvec3 = glm::uvec3;
using uvec4 = glm::uvec4;

/// GLM uint8 vector aliases — used for packed per-byte RGBA.
using u8vec2 = glm::u8vec2;
using u8vec3 = glm::u8vec3;
using u8vec4 = glm::u8vec4;

/// GLM uint32 vector aliases.
using u32vec1 = glm::u32vec1;
using u32vec2 = glm::u32vec2;

/// GLM matrix aliases.
using mat2 = glm::mat2;
using mat3 = glm::mat3;
using mat4 = glm::mat4;

/// Which of a voxel's three visible isometric faces a triangle belongs to.
/// Each voxel in the iso view exposes an X face (left), Y face (right),
/// and Z face (top). NONE_FACE indicates an out-of-bounds or unclassified
/// triangle index.
enum FaceType { NONE_FACE, X_FACE, Y_FACE, Z_FACE };

/// The six face slots of a rectangular voxel prism: two per axis
/// (near/far in view space). X1/Y1/Z1 are the front-facing sides;
/// X2/Y2/Z2 are the back-facing sides (hidden in a solid object).
enum Faces { X1, X2, Y1, Y2, Z1, Z2 };

/// Canonical 3D axis selector used by layout helpers and the helix/arc
/// path generators to choose which world axis becomes the primary motion
/// direction.
enum class CoordinateAxis { XAxis = 0, YAxis = 1, ZAxis = 2 };

/// One-dimensional curve shapes for path generation.
enum Shape1D { LINE, CURVED_LINE, NONE_SHAPE_1D };

/// Two-dimensional geometric shape classifiers.
enum Shape2D { RECTANGLE, TRIANGLE, CIRCLE, TRAPEZOID, NONE_SHAPE_2D };

/// Three-dimensional shape classifiers.
enum class Shape3D { RECTANGULAR_PRISM, SPHERE, NONE_SHAPE_3D };

/// Which 2D iso plane a layout helper operates in. Determines which world
/// axis becomes "depth" (invisible in the 2D projection):
/// - XY: motion in X and Y, depth on Z
/// - XZ: motion in X and Z, depth on Y
/// - YZ: motion in Y and Z, depth on X
enum class PlaneIso {
    XY = 0, ///< depth on Z
    XZ = 1, ///< depth on Y
    YZ = 2  ///< depth on X
};

/// Floating-point HSV + alpha color. Hue in [0, 1), saturation and value
/// in [0, 1]. Convert to `ColorHSV` via `colorToColorHSV`; convert back to
/// `Color` via `colorHSVToColor`.
struct ColorHSV {
    float hue_;
    float saturation_;
    float value_;
    float alpha_;
};

/// RGBA color stored as four uint8 bytes. The engine's canonical color type
/// for trixel canvas pixels and ECS color components.
struct Color {
    uint8_t red_;
    uint8_t green_;
    uint8_t blue_;
    uint8_t alpha_;

    /// Pack the four channels into a single little-endian RGBA uint32:
    /// bits [7:0]=R, [15:8]=G, [23:16]=B, [31:24]=A.
    constexpr std::uint32_t toPackedRGBA() const {
        return static_cast<std::uint32_t>(red_) |
               (static_cast<std::uint32_t>(green_) << 8) |
               (static_cast<std::uint32_t>(blue_) << 16) |
               (static_cast<std::uint32_t>(alpha_) << 24);
    }
};

/// Draw-order depth scalar: `x + y + z`. Higher values are further from the
/// camera. Computed by `pos3DtoDistance`; used to sort voxels in the depth
/// buffer.
using Distance = int32_t;

/// Well-known named color constants. All are fully opaque (alpha=0xFF) except
/// `kInvisable` (alpha=0x00).
namespace IRColors {
constexpr Color kInvisable = Color{0x00, 0x00, 0x00, 0x00}; ///< Fully transparent black.
constexpr Color kBlack = Color{0x00, 0x00, 0x00, 0xFF};
constexpr Color kWhite = Color{0xFF, 0xFF, 0xFF, 0xFF};
constexpr Color kRed = Color{0xFF, 0x00, 0x00, 0xFF};
constexpr Color kGreen = Color{0x00, 0xFF, 0x00, 0xFF};
constexpr Color kBlue = Color{0x00, 0x00, 0xFF, 0xFF};
constexpr Color kYellow = Color{0xFF, 0xFF, 0x00, 0xFF};
} // namespace IRColors

} // namespace IRMath

#endif /* IR_MATH_TYPES_H */
