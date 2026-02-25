#ifndef IR_MATH_TYPES_H
#define IR_MATH_TYPES_H

#define GLM_ENABLE_EXPERIMENTAL

#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <glm/mat2x2.hpp>
#include <glm/vector_relational.hpp>
#include <glm/gtx/color_space.hpp>

namespace IRMath {
using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;

using dvec2 = glm::dvec2;
using dvec3 = glm::dvec3;
using dvec4 = glm::dvec4;

using ivec1 = glm::ivec1;
using ivec2 = glm::ivec2;
using ivec3 = glm::ivec3;
using ivec4 = glm::ivec4;

using uvec2 = glm::uvec2;
using uvec3 = glm::uvec3;
using uvec4 = glm::uvec4;

using u8vec2 = glm::u8vec2;
using u8vec3 = glm::u8vec3;
using u8vec4 = glm::u8vec4;

using u32vec1 = glm::u32vec1;
using u32vec2 = glm::u32vec2;

using mat2 = glm::mat2;
using mat3 = glm::mat3;
using mat4 = glm::mat4;

enum FaceType { NONE_FACE, X_FACE, Y_FACE, Z_FACE };

// const std::unordered_map<FaceType, ivec3> kFaceCameraRotations {
//     {NONE_FACE, ivec3(0, 0, 0)},
//     {X_FACE, }
// }

enum Faces { X1, X2, Y1, Y2, Z1, Z2 };

enum class CoordinateAxis { XAxis, YAxis, ZAzis };

// Can be used for 2DIso, when given a heading/direction, and some
// points
enum Shape1D { LINE, CURVED_LINE, NONE_SHAPE_1D };

enum Shape2D { RECTANGLE, TRIANGLE, CIRCLE, TRAPEZOID, NONE_SHAPE_2D };

enum class Shape3D { RECTANGULAR_PRISM, SPHERE, NONE_SHAPE_3D };

enum class PlaneIso {
    XY = 0, // depth on Z
    XZ = 1, // depth on Y
    YZ = 2  // depth on X
};

struct ColorHSV {
    float hue_;
    float saturation_;
    float value_;
    float alpha_;
};

struct Color {
    uint8_t red_;
    uint8_t green_;
    uint8_t blue_;
    uint8_t alpha_;
};
using Distance = int32_t;

namespace IRColors {
constexpr Color kInvisable = Color{0x00, 0x00, 0x00, 0x00};
constexpr Color kBlack = Color{0x00, 0x00, 0x00, 0xFF};
constexpr Color kWhite = Color{0xFF, 0xFF, 0xFF, 0xFF};
constexpr Color kRed = Color{0xFF, 0x00, 0x00, 0xFF};
constexpr Color kGreen = Color{0x00, 0xFF, 0x00, 0xFF};
constexpr Color kBlue = Color{0x00, 0x00, 0xFF, 0xFF};
constexpr Color kYellow = Color{0xFF, 0xFF, 0x00, 0xFF};
} // namespace IRColors

} // namespace IRMath

#endif /* IR_MATH_TYPES_H */
