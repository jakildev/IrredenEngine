#ifndef IR_MATH_H
#define IR_MATH_H

#include <irreden/math/ir_math_types.hpp>
#include <irreden/math/easing_functions.hpp>
#include <irreden/math/color_palettes.hpp>
#include <irreden/math/color.hpp>
#include <irreden/math/physics.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <vector>

// TODO: The game engine needs transformations for voxel sets
// rotation in 3D space

namespace IRMath {

std::vector<Color> createColorPaletteFromFile(const char *filename);

const bool randomBool();
const int randomInt(const int min, const int max);
const float randomFloat(const float min, const float max);
const Color randomColor();
const Color randomColor(const std::vector<Color> &colorPalette);

const vec3 randomVec(const vec3 min, const vec3 max);

constexpr int round(float value) {
    return glm::round(value);
}

constexpr ivec2 roundVec(vec2 value) {
    return ivec2(round(value.x), round(value.y));
}

template <typename VecType> constexpr VecType max(const VecType &vector1, const VecType &vector2) {
    return glm::max(vector1, vector2);
}

template <typename VecType> constexpr VecType normalize(const VecType &vector) {
    return glm::normalize(vector);
}

template <typename VecType> constexpr auto length(const VecType &vector) {
    return glm::length(vector);
}

template <typename T> constexpr T min(const T &value1, const T &value2) {
    return glm::min(value1, value2);
}

template <typename T> constexpr T abs(const T &value) {
    return glm::abs(value);
}

template <typename T> constexpr T clamp(const T &value, const T &minValue, const T &maxValue) {
    return glm::clamp(value, minValue, maxValue);
}

template <typename T, typename U> constexpr T mix(const T &value1, const T &value2, const U &t) {
    return glm::mix(value1, value2, t);
}

template <typename T> constexpr auto dot(const T &value1, const T &value2) {
    return glm::dot(value1, value2);
}

template <typename T> constexpr auto lessThan(const T &value1, const T &value2) {
    return glm::lessThan(value1, value2);
}

template <typename T> constexpr auto greaterThanEqual(const T &value1, const T &value2) {
    return glm::greaterThanEqual(value1, value2);
}

template <typename T> constexpr bool all(const T &value) {
    return glm::all(value);
}

template <typename T> constexpr auto floor(const T &value) {
    return glm::floor(value);
}

template <typename T> constexpr auto ceil(const T &value) {
    return glm::ceil(value);
}

template <typename T> constexpr auto fract(const T &value) {
    return glm::fract(value);
}

constexpr float sin(float value) {
    return glm::sin(value);
}

inline mat4 ortho(float left, float right, float bottom, float top, float nearZ, float farZ) {
    return glm::ortho(left, right, bottom, top, nearZ, farZ);
}

inline mat4 translate(const mat4 &matrix, const vec3 &position) {
    return glm::translate(matrix, position);
}

inline mat4 scale(const mat4 &matrix, const vec3 &value) {
    return glm::scale(matrix, value);
}

constexpr int sumVecComponents(const ivec2 value) {
    return value.x + value.y;
}

constexpr int sumVecComponents(const ivec3 value) {
    return value.x + value.y + value.z;
}

constexpr int sumVecComponents(const vec3 value) {
    return value.x + value.y + value.z;
}

constexpr int sumVecComponents(const vec2 value) {
    return value.x + value.y;
}

constexpr int multVecComponents(const ivec3 value) {
    return value.x * value.y * value.z;
}

constexpr ivec2 size3DtoOriginOffset2DX1(const uvec3 size) {
    return ivec2(size.x, size.x + size.y - 1);
}
constexpr ivec2 size3DtoOriginOffset2DY1(const uvec3 size) {
    return size3DtoOriginOffset2DX1(size) - ivec2(1, 0);
}
constexpr ivec2 size3DtoOriginOffset2DZ1(const uvec3 size) {
    return size3DtoOriginOffset2DX1(size) - ivec2(1, 1);
}

constexpr ivec2 pos3DtoPos2DIso(const ivec3 position) {
    return ivec2(-position.x + position.y, -position.x - position.y + (2 * position.z));
}

constexpr vec2 pos3DtoPos2DIso(const vec3 position) {
    return vec2(-position.x + position.y, -position.x - position.y + (2 * position.z));
}

constexpr vec2 pos3DtoPos2DScreen(const vec3 position, const vec2 triangleStepSizeScreen) {
    return pos3DtoPos2DIso(position) * triangleStepSizeScreen * vec2(-1.0f);
}

// Shift a 3D position along the isometric depth axis (1,1,1).
// The returned position projects to the same 2D iso location
// but sits at a different depth.  Positive values move "behind"
// (further from the camera); negative values move "in front".
constexpr vec3 isoDepthShift(const vec3 &position, float depth) {
    return position + vec3(depth);
}

// constexpr vec2 pos3DtoPos2DScreenOffset(const vec3 position) {
//     return pos3DtoPos2DIso(position) ...
// }

// all positions are from screen center (0, 0, 0)
constexpr vec2 pos2DScreenToPos2DIso(const vec2 screenPos, const vec2 triangleStepSizeScreen) {
    return screenPos / triangleStepSizeScreen;
}

constexpr vec2
offsetScreenToIsoTriangles(const vec2 offsetScreen, const vec2 triangleStepSizeScreen) {
    return offsetScreen / triangleStepSizeScreen;
}

// constexpr vec2 pos3DtoPos2DScreen(
//     const vec3 position,
//     const vec2 triangleStepSize
// )
// {

// }

vec2 symmetricRound(const vec2 &input);

constexpr Distance pos3DtoDistance(const ivec3 position) {
    return sumVecComponents(position);
}
constexpr Distance pos3DtoDistance(const vec3 position) {
    return round(sumVecComponents(position));
}

template <ivec3 size> constexpr FaceType calcFaceTypeFromTriangleIndexAndSize(const ivec2 index) {
    ivec2 origin = size3DtoOriginOffset2DX1(size);
    ivec2 offsetPosition = index - origin;

    if (offsetPosition.x >= 0 && offsetPosition.x >= -offsetPosition.y &&
        (offsetPosition.x + offsetPosition.y) < size.z * 2) {
        return FaceType::X_FACE;
    }

    if (offsetPosition.x < 0 && offsetPosition.x < offsetPosition.y &&
        (offsetPosition.y - offsetPosition.x) <= size.z * 2) {
        return FaceType::Y_FACE;
    }

    if ((((index.x >= size.x) && ((offsetPosition.x + offsetPosition.y) < 0)) ||
         ((index.x < size.x) && ((-offsetPosition.x + offsetPosition.y) <= 0))) &&
        (((index.x >= size.y) && ((index.x - index.y) < size.y)) ||
         ((index.x < size.y) && ((index.x + index.y) >= size.y)))) {
        return FaceType::Z_FACE;
    }

    return FaceType::NONE_FACE;
}

template <uvec3 size> constexpr ivec3 pos2DIsoToPos3DRectSurface(const ivec2 position) {
    ivec2 origin = size3DtoOriginOffset2DX1(size);
    ivec2 positionFromOrigin = position - origin;
    FaceType faceType = calcFaceTypeFromTriangleIndexAndSize<size>(position);

    if (faceType == FaceType::X_FACE) {
        return ivec3(0, positionFromOrigin.x, (positionFromOrigin.x + positionFromOrigin.y) / 2);
    }

    if (faceType == FaceType::Y_FACE) {
        return ivec3(
            -positionFromOrigin.x - 1,
            0,
            (-positionFromOrigin.x - 1 + positionFromOrigin.y) / 2
        );
    }

    if (faceType == FaceType::Z_FACE) {
        return ivec3(
            -(positionFromOrigin.x + positionFromOrigin.y + 1) / 2,
            (positionFromOrigin.x - positionFromOrigin.y) / 2,
            0
        );
    }

    return ivec3(-1, -1, -1);
}

constexpr vec2 pos2DIsoToPos2DGameResolution(const vec2 position, const vec2 zoomLevel) {
    return position * zoomLevel * vec2(2, 1);
}

// Selects from bottom Z face
template <ivec3 size>
constexpr ivec3 pos2DIsoToPos3DAtZLevel(const ivec2 position, const int zLevel) {
    // Origin set to lower right Z face at correct Z level
    ivec2 origin = size3DtoOriginOffset2DX1(size) + ivec2(-1, 0) + ivec2(0, zLevel * 2);
    ivec2 positionFromOrigin = position - origin;
    return ivec3(
        glm::ceil(-(positionFromOrigin.x + positionFromOrigin.y) / 2.0),
        (positionFromOrigin.x - positionFromOrigin.y) / 2,
        zLevel
    );
}

// Selects from bottom Z face
constexpr ivec3 pos2DIsoToPos3DAtZLevelNew(const ivec2 positionFromOrigin, const int zLevel) {
    ivec2 positionZLevelAdjusted = positionFromOrigin - ivec2(0, zLevel * 2);
    return ivec3(
        glm::ceil(-(positionZLevelAdjusted.x + positionZLevelAdjusted.y) / 2.0),
        (positionZLevelAdjusted.x - positionZLevelAdjusted.y) / 2,
        zLevel
    );
}

// Alt from above TODO remove above
constexpr ivec3 pos2DIsoToPos3DAtZLevelAlt(const ivec2 position, const int zLevel) {
    return ivec3(
        glm::ceil(-(position.x + position.y) / 2.0),
        (position.x - position.y) / 2,
        zLevel
    );
}

constexpr int index2DtoIndex1D(const ivec2 index, const ivec2 size) {
    return index.y * size.x + index.x;
}

constexpr int index3DtoIndex1D(const ivec3 index, const ivec3 size) {
    return index.z * size.x * size.y + index.y * size.x + index.x;
}

// ISOMETRIC THINGS

template <FaceType faceType> constexpr ivec2 calculatePartnerTriangleIndex(ivec2 index);

template <> constexpr ivec2 calculatePartnerTriangleIndex<FaceType::X_FACE>(ivec2 index) {
    if (IRMath::sumVecComponents(index) % 2 == 0) {
        return index + ivec2(0, -1);
    }

    return index + ivec2(0, 1);
}

template <> constexpr ivec2 calculatePartnerTriangleIndex<FaceType::Y_FACE>(ivec2 index) {
    if (IRMath::sumVecComponents(index) % 2 == 0) {
        return index + ivec2(0, 1);
    }

    return index + ivec2(0, -1);
}

template <> constexpr ivec2 calculatePartnerTriangleIndex<FaceType::Z_FACE>(ivec2 index) {
    if (IRMath::sumVecComponents(index) % 2 == 0) {
        return index + ivec2(-1, 0);
    }

    return index + ivec2(1, 0);
}

constexpr ivec2 size3DtoSize2DIso(const ivec3 size) {
    return ivec2(
        size.x + size.y,
        (size.x + size.y) + (size.z * 2) - 1 // TODO: check this
    );
}

constexpr uvec2 gameResolutionToSize2DIso(const uvec2 gameResolution, const uvec2 scaleFactor) {
    // Floor division
    return gameResolution / uvec2(2, 1) / scaleFactor;
}

constexpr vec2
gameResolutionToSize2DIso(const vec2 gameResolution, const vec2 scaleFactor = vec2(1.0f)) {
    // Floor division
    return gameResolution / vec2(2, 1) / scaleFactor;
}

constexpr ivec2 calcTriangleStepSizeScreen(
    const vec2 gameResolution, const vec2 zoomLevel, const ivec2 pixelScaleFactor
) {
    return (
        ivec2(gameResolution / gameResolutionToSize2DIso(gameResolution, zoomLevel)) *
        pixelScaleFactor
    );
}

constexpr ivec2
calcTriangleStepSizeGameResolution(const vec2 gameResolution, const vec2 zoomLevel) {
    return calcTriangleStepSizeScreen(gameResolution, zoomLevel, ivec2(1));
}

constexpr uvec2 size2DIsoToGameResolution(const uvec2 size, const uvec2 scaleFactor) {
    // Floor division (THIS IS UNTESTED)
    return size / uvec2(1, 2) * scaleFactor;
}

vec2 pos2DIsoToTriangleIndex(const vec2 position, const ivec2 triangleStepSizeScreen);

float fract(float value);

vec3 hsvToRgb(const vec3 &colorHSV);

u8vec3 hsvToRgbBytes(const vec3 &colorHSV);

constexpr int
calcResolutionWidthFromHeightAndAspectRatio(const int height, const ivec2 aspectRatio) {
    return static_cast<int>(height * static_cast<float>(aspectRatio.y) / aspectRatio.x);
}

constexpr int
calcResolutionHeightFromWidthAndAspectRatio(const int width, const ivec2 aspectRatio) {
    return static_cast<int>(width * static_cast<float>(aspectRatio.x) / aspectRatio.y);
}

// Float to byte rounding (clamped to prevent uint8 overflow/wrap)
constexpr uint8_t roundFloatToByte(const float value) {
    const float clamped = clamp(value, 0.0f, 1.0f);
    return static_cast<uint8_t>(round(clamped * 255.0f));
}

constexpr float roundByteToFloat(const uint8_t value) {
    return (float)value / 255.0f;
}

constexpr uint8_t lerpByte(uint8_t from, uint8_t to, float t) {
    const float tClamped = clamp(t, 0.0f, 1.0f);
    const float fromFloat = roundByteToFloat(from);
    const float toFloat = roundByteToFloat(to);
    return roundFloatToByte(mix(fromFloat, toFloat, tClamped));
}

constexpr Color lerpColor(const Color &from, const Color &to, float t) {
    return Color{
        lerpByte(from.red_, to.red_, t),
        lerpByte(from.green_, to.green_, t),
        lerpByte(from.blue_, to.blue_, t),
        lerpByte(from.alpha_, to.alpha_, t)
    };
}

constexpr ivec3 roundVec3ToIVec3(vec3 value) {
    return ivec3(round(value.x), round(value.y), round(value.z));
}

constexpr ivec2 trixelOriginOffsetX1(const ivec2 &trixelCanvasSize) {
    return trixelCanvasSize / ivec2(2);
}
constexpr ivec2 trixelOriginOffsetX2(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize) + ivec2(0, 1);
}
constexpr ivec2 trixelOriginOffsetY1(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize) + ivec2(-1, 0);
}
constexpr ivec2 trixelOriginOffsetY2(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize) + ivec2(-1, 1);
}
constexpr ivec2 trixelOriginOffsetZ1(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize) + ivec2(-1, -1);
}
constexpr ivec2 trixelOriginOffsetZ2(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize) + ivec2(0, -1);
}
// back faces
constexpr ivec2 trixelOriginOffsetX3(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetZ1(trixelCanvasSize);
}
constexpr ivec2 trixelOriginOffsetX4(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetY1(trixelCanvasSize);
}
constexpr ivec2 trixelOriginOffsetY3(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetZ2(trixelCanvasSize);
}
constexpr ivec2 trixelOriginOffsetY4(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize);
}
constexpr ivec2 trixelOriginOffsetZ3(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetY2(trixelCanvasSize);
}
constexpr ivec2 trixelOriginOffsetZ4(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetX2(trixelCanvasSize);
}

Color colorHSVToColor(const ColorHSV &colorHSV);

ColorHSV colorToColorHSV(const Color &color);

// Layout helpers for scriptable entity placement.
// plane values:
//   0 = XY plane (depth along Z)
//   1 = XZ plane (depth along Y)
//   2 = YZ plane (depth along X)
vec3 layoutGridCentered(
    int index,
    int count,
    int columns,
    float spacingPrimary,
    float spacingSecondary,
    int plane = 0,
    float depth = 0.0f
);

vec3 layoutZigZagCentered(
    int index,
    int count,
    int itemsPerZag,
    float spacingPrimary,
    float spacingSecondary,
    int plane = 0,
    float depth = 0.0f
);

vec3 layoutZigZagPath(
    int index,
    int count,
    int itemsPerSegment,
    float spacingPrimary,
    float spacingSecondary,
    int plane = 0,
    float depth = 0.0f
);

vec3 layoutSquareSpiral(
    int index,
    float spacing,
    int plane = 0,
    float depth = 0.0f
);

vec3 layoutHelix(
    int index,
    int count,
    float radius,
    float turns,
    float heightSpan,
    int axis = 2
);

// 2D

// i^ grid to screen
template <vec2 objSize> constexpr vec2 kIHatGridToScreenIso = vec2(1.0f, 0.5f) * (objSize / 2.0f);

// j^ grid to screen
template <vec2 objSize> constexpr vec2 kJHatGridToScreenIso = vec2(-1.0f, 0.5f) * (objSize / 2.0f);

// xy matrix grid to screen
template <vec2 iHat, vec2 jHat>
constexpr mat2 k2DGridToScreenIsoTransform =
    // TODO: is this order correct??
    mat2(iHat.x, jHat.x, iHat.y, jHat.y);

// 3D

// Isometric traversal (these cycle)

// X = 0 face (left)

constexpr ivec3 kRaymarchStepsXFaceLower[] = {
    ivec3(0, 1, 0),
    ivec3(1, 0, 0),
    ivec3(0, 0, 1),
    ivec3(0, 1, 0),
    ivec3(1, 0, 0),
};

constexpr ivec3 kRaymarchStepXFaceUpper[] = {
    ivec3(0, 1, 0),
    ivec3(1, 0, 0),
    ivec3(0, 1, 0),
    ivec3(0, 0, 1),
    ivec3(1, 0, 0),
};

constexpr ivec3 kRaymarchStepYFaceUpper[] = {
    ivec3(1, 0, 0),
    ivec3(0, 1, 0),
    ivec3(1, 0, 0),
    ivec3(0, 0, 1),
    ivec3(0, 1, 0),
};

constexpr ivec3 kRaymarchStepsYFaceLower[] = {
    ivec3(1, 0, 0), ivec3(0, 1, 0), ivec3(0, 0, 1), ivec3(1, 0, 0), ivec3(0, 1, 0)
};

constexpr ivec3 kRaymarchStepsZFaceLeft[] = {
    ivec3(1, 0, 0),
    ivec3(0, 1, 0),
    ivec3(1, 0, 0),
    ivec3(0, 1, 0),
    ivec3(0, 0, 1),
};

constexpr ivec3 kRaymarchStepsZFaceRight[] = {
    ivec3(0, 1, 0),
    ivec3(1, 0, 0),
    ivec3(0, 1, 0),
    ivec3(1, 0, 0),
    ivec3(0, 0, 1),
};

template <int FPS> constexpr int secondsToFrames(float seconds) {
    return ceil(seconds * FPS);
}

} // namespace IRMath

#endif /* IR_MATH_H */
