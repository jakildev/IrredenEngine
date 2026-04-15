#ifndef IR_MATH_H
#define IR_MATH_H

#include <irreden/math/ir_math_types.hpp>
#include <irreden/math/easing_functions.hpp>
#include <irreden/math/color_palettes.hpp>
#include <irreden/math/color.hpp>
#include <irreden/math/physics.hpp>
#include <irreden/ir_platform.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <vector>

// TODO: The game engine needs transformations for voxel sets
// rotation in 3D space

namespace IRMath {

/// Loads a color palette from a file and returns it as a vector of Colors.
std::vector<Color> createColorPaletteFromFile(const char *filename);

/// Returns a uniformly random bool.
const bool randomBool();
/// Returns a uniformly random integer in [min, max].
const int randomInt(const int min, const int max);
/// Returns a uniformly random float in [min, max].
const float randomFloat(const float min, const float max);
/// Returns a random RGBA color with fully-opaque alpha.
const Color randomColor();
/// Returns a random color sampled from @p colorPalette.
const Color randomColor(const std::vector<Color> &colorPalette);

/// Returns a random vec3 with each component in [min, max].
const vec3 randomVec(const vec3 min, const vec3 max);

/// Rounds @p value to the nearest integer.
constexpr int round(float value) {
    return glm::round(value);
}

/// Ceiling division: equivalent to ceil(numerator / denominator) without
/// floating-point arithmetic.
constexpr int divCeil(int numerator, int denominator) {
    return (numerator + denominator - 1) / denominator;
}

/// Rounds each component of @p value to the nearest integer.
constexpr ivec2 roundVec(vec2 value) {
    return ivec2(round(value.x), round(value.y));
}

/// Component-wise maximum. GLM wrapper.
template <typename VecType> constexpr VecType max(const VecType &vector1, const VecType &vector2) {
    return glm::max(vector1, vector2);
}

/// Returns a unit vector in the direction of @p vector. GLM wrapper.
template <typename VecType> constexpr VecType normalize(const VecType &vector) {
    return glm::normalize(vector);
}

/// Returns the Euclidean length of @p vector. GLM wrapper.
template <typename VecType> constexpr auto length(const VecType &vector) {
    return glm::length(vector);
}

/// Scalar or component-wise minimum. GLM wrapper.
template <typename T> constexpr T min(const T &value1, const T &value2) {
    return glm::min(value1, value2);
}

/// Scalar or component-wise absolute value. GLM wrapper.
template <typename T> constexpr T abs(const T &value) {
    return glm::abs(value);
}

/// Clamps @p value to [minValue, maxValue] (scalar or component-wise).
/// GLM wrapper.
template <typename T> constexpr T clamp(const T &value, const T &minValue, const T &maxValue) {
    return glm::clamp(value, minValue, maxValue);
}

/// Linear interpolation from @p value1 to @p value2 at parameter @p t
/// (scalar or component-wise). GLM wrapper.
template <typename T, typename U> constexpr T mix(const T &value1, const T &value2, const U &t) {
    return glm::mix(value1, value2, t);
}

/// Dot product of @p value1 and @p value2. GLM wrapper.
template <typename T> constexpr auto dot(const T &value1, const T &value2) {
    return glm::dot(value1, value2);
}

/// Component-wise less-than comparison. GLM wrapper.
template <typename T> constexpr auto lessThan(const T &value1, const T &value2) {
    return glm::lessThan(value1, value2);
}

/// Component-wise greater-than-or-equal comparison. GLM wrapper.
template <typename T> constexpr auto greaterThanEqual(const T &value1, const T &value2) {
    return glm::greaterThanEqual(value1, value2);
}

/// Returns true if all boolean components of @p value are true. GLM wrapper.
template <typename T> constexpr bool all(const T &value) {
    return glm::all(value);
}

/// Component-wise floor. GLM wrapper.
template <typename T> constexpr auto floor(const T &value) {
    return glm::floor(value);
}

/// Component-wise ceiling. GLM wrapper.
template <typename T> constexpr auto ceil(const T &value) {
    return glm::ceil(value);
}

/// Component-wise fractional part (always in [0, 1)). GLM wrapper.
template <typename T> constexpr auto fract(const T &value) {
    return glm::fract(value);
}

/// Fractional part of the absolute value; always in [0, 1). Ignores sign.
template <typename T> constexpr auto fractAbs(const T &value) {
    return glm::fract(glm::abs(value));
}

/// Sine of @p value (radians). GLM wrapper.
constexpr float sin(float value) {
    return glm::sin(value);
}

/// Orthographic projection matrix.  Selects the depth range convention
/// ([0,1] for Metal/Vulkan, [-1,1] for OpenGL) from IRPlatform::kGfx.
inline mat4 ortho(float left, float right, float bottom, float top, float nearZ, float farZ) {
    if constexpr (IRPlatform::kGfx.ndcDepthZeroToOne_) {
        return glm::orthoZO(left, right, bottom, top, nearZ, farZ);
    } else {
        return glm::orthoNO(left, right, bottom, top, nearZ, farZ);
    }
}

/// Translation matrix that moves @p matrix by @p position. GLM wrapper.
inline mat4 translate(const mat4 &matrix, const vec3 &position) {
    return glm::translate(matrix, position);
}

/// Scaling matrix that scales @p matrix by @p value. GLM wrapper.
inline mat4 scale(const mat4 &matrix, const vec3 &value) {
    return glm::scale(matrix, value);
}

/// Sum of all components of @p value.
constexpr int sumVecComponents(const ivec2 value) {
    return value.x + value.y;
}

/// Sum of all components of @p value.
constexpr int sumVecComponents(const ivec3 value) {
    return value.x + value.y + value.z;
}

/// Sum of all components of @p value.
constexpr int sumVecComponents(const vec3 value) {
    return value.x + value.y + value.z;
}

/// Sum of all components of @p value.
constexpr int sumVecComponents(const vec2 value) {
    return value.x + value.y;
}

/// Product of all components of @p value.
constexpr int multVecComponents(const ivec3 value) {
    return value.x * value.y * value.z;
}

/// Canvas origin offset for the primary X face (parity 1).
/// The offset maps the world origin (0,0,0) to a canvas pixel near the
/// centre of the trixel canvas; each face type has a different sub-pixel
/// alignment due to the isometric trixel grid layout.
constexpr ivec2 size3DtoOriginOffset2DX1(const uvec3 size) {
    return ivec2(size.x, size.x + size.y - 1);
}
/// Canvas origin offset for the primary Y face (parity 1).
constexpr ivec2 size3DtoOriginOffset2DY1(const uvec3 size) {
    return size3DtoOriginOffset2DX1(size) - ivec2(1, 0);
}
/// Canvas origin offset for the primary Z face (parity 1).
constexpr ivec2 size3DtoOriginOffset2DZ1(const uvec3 size) {
    return size3DtoOriginOffset2DX1(size) - ivec2(1, 1);
}

/// Projects a 3D world position to a 2D isometric canvas position.
///
/// The isometric equations are:
/// @code
///   iso.x = -x + y
///   iso.y = -x - y + 2z
/// @endcode
///
/// The result is a canvas pixel offset from the canvas origin.  To obtain the
/// final canvas pixel, add the canvas origin offset and floor(cameraIso):
/// @code
///   canvasPixel = canvasOriginOffset + floor(cameraIso) + pos3DtoPos2DIso(world)
/// @endcode
///
/// Iso Y increases upward; the backend's canvas-to-screen flip is applied
/// outside these helpers.  **Never inline these equations** — always use
/// this helper so there is one place to fix coordinate-system bugs.
constexpr ivec2 pos3DtoPos2DIso(const ivec3 position) {
    return ivec2(-position.x + position.y, -position.x - position.y + (2 * position.z));
}

/// @overload vec3 variant returning a floating-point iso position.
constexpr vec2 pos3DtoPos2DIso(const vec3 position) {
    return vec2(-position.x + position.y, -position.x - position.y + (2 * position.z));
}

/// Projects @p position to screen space by scaling the iso result by
/// @p triangleStepSizeScreen and applying the backend-specific Y sign from
/// IRPlatform::kGfx.
constexpr vec2 pos3DtoPos2DScreen(const vec3 position, const vec2 triangleStepSizeScreen) {
    return pos3DtoPos2DIso(position) * triangleStepSizeScreen *
           vec2(-1.0f, IRPlatform::kGfx.screenYDirection_);
}

/// Shifts @p position along the isometric depth axis (1, 1, 1) by @p depth
/// units.  The returned position projects to the same 2D iso location but
/// sits at a different depth.  Positive values move further from the camera;
/// negative values move closer.
constexpr vec3 isoDepthShift(const vec3 &position, float depth) {
    return position + vec3(depth);
}

/// 2D axis-aligned bounding box in isometric canvas space.
struct IsoBounds2D {
    vec2 min_;
    vec2 max_;

    /// Constructs an IsoBounds2D that encloses @p cornerA and @p cornerB.
    static IsoBounds2D fromCorners(vec2 cornerA, vec2 cornerB) {
        return {
            vec2(glm::min(cornerA.x, cornerB.x), glm::min(cornerA.y, cornerB.y)),
            vec2(glm::max(cornerA.x, cornerB.x), glm::max(cornerA.y, cornerB.y))
        };
    }

    /// Returns true if @p point lies within [min_, max_] (inclusive).
    bool contains(vec2 point) const {
        return point.x >= min_.x && point.x <= max_.x &&
               point.y >= min_.y && point.y <= max_.y;
    }

    /// Returns the centre of the bounds.
    vec2 center() const { return (min_ + max_) * 0.5f; }
    /// Returns the size of the bounds (max_ - min_).
    vec2 extent() const { return max_ - min_; }
};

/// Returns the visible iso-space viewport rectangle given camera and canvas
/// parameters.
///
/// Inverts the canvas-pixel formula:
/// @code
///   canvasPixel = canvasOriginOffset + floor(cameraIso) + isoPos
/// @endcode
///
/// At zoom Z, only the centre canvasSize / Z fraction of the canvas is
/// on screen (the trixel-to-framebuffer model matrix scales by
/// resolution × zoom, so pixels outside the centre 1/Z fraction are
/// off-screen).  @p margin adds an optional pixel guard band.
inline IsoBounds2D visibleIsoViewport(
    vec2 cameraIso,
    ivec2 canvasOriginOffset,
    ivec2 canvasSize,
    vec2 zoom = vec2(1.0f),
    int margin = 0
) {
    vec2 viewCenter = -vec2(canvasOriginOffset)
                      - vec2(glm::floor(cameraIso.x), glm::floor(cameraIso.y))
                      + vec2(canvasSize) * 0.5f;
    vec2 halfExtent = vec2(canvasSize) / (zoom * 2.0f);
    return {
        viewCenter - halfExtent - vec2(margin),
        viewCenter + halfExtent + vec2(margin)
    };
}

/// Returns a conservative iso-space half-extent for a rectangular-prism
/// entity of @p voxelSize dimensions.  Cheaper than the 8-corner
/// entityIsoBounds enumeration but slightly overestimates (ceil + 1 per axis).
inline vec2 shapeIsoHalfExtent(vec3 voxelSize) {
    vec3 halfSize = voxelSize * 0.5f;
    float extentX = glm::ceil(halfSize.x) + 1.0f;
    float extentY = glm::ceil(halfSize.y) + 1.0f;
    float extentZ = glm::ceil(halfSize.z) + 1.0f;
    return vec2(extentX + extentY, extentX + extentY + 2.0f * extentZ);
}

/// Returns the tight iso-space axis-aligned bounding box of a rectangular
/// prism entity at @p worldPos with @p voxelSize dimensions.  Enumerates
/// all 8 corners for an exact result (prefer shapeIsoHalfExtent when a
/// conservative approximation is acceptable).
inline IsoBounds2D entityIsoBounds(vec3 worldPos, ivec3 voxelSize) {
    vec2 corners[8];
    int idx = 0;
    for (int dx = 0; dx <= 1; ++dx) {
        for (int dy = 0; dy <= 1; ++dy) {
            for (int dz = 0; dz <= 1; ++dz) {
                vec3 corner = worldPos + vec3(
                    dx * voxelSize.x,
                    dy * voxelSize.y,
                    dz * voxelSize.z
                );
                corners[idx++] = pos3DtoPos2DIso(corner);
            }
        }
    }
    vec2 bmin = corners[0];
    vec2 bmax = corners[0];
    for (int i = 1; i < 8; ++i) {
        bmin = glm::min(bmin, corners[i]);
        bmax = glm::max(bmax, corners[i]);
    }
    return IsoBounds2D{bmin, bmax};
}

/// Returns true if a rectangular-prism entity's iso bounding box overlaps
/// the trixel canvas.  Converts the world AABB to iso, translates to canvas
/// pixels, and tests against [0, canvasSize).
inline bool isEntityOnScreen(
    vec3 worldPos,
    ivec3 voxelSize,
    vec2 cameraIso,
    ivec2 canvasOffsetZ1,
    ivec2 canvasSize
) {
    IsoBounds2D bounds = entityIsoBounds(worldPos, voxelSize);
    vec2 canvasMin = bounds.min_ + vec2(canvasOffsetZ1) + vec2(glm::floor(cameraIso.x), glm::floor(cameraIso.y));
    vec2 canvasMax = bounds.max_ + vec2(canvasOffsetZ1) + vec2(glm::floor(cameraIso.x), glm::floor(cameraIso.y));
    return canvasMax.x >= 0 && canvasMin.x < canvasSize.x &&
           canvasMax.y >= 0 && canvasMin.y < canvasSize.y;
}

/// Converts a screen-centre-relative position to iso space by dividing by
/// @p triangleStepSizeScreen.
constexpr vec2 pos2DScreenToPos2DIso(const vec2 screenPos, const vec2 triangleStepSizeScreen) {
    return screenPos / triangleStepSizeScreen;
}

/// Converts a screen-space delta to an iso-space delta.  The backend-specific
/// Y sign from IRPlatform::kGfx is applied automatically.
constexpr vec2 screenDeltaToIsoDelta(
    const vec2 screenDelta, const vec2 triangleStepSizeScreen
) {
    return screenDelta / triangleStepSizeScreen * vec2(1.0f, IRPlatform::kGfx.screenYDirection_);
}

/// Converts an iso-space delta to a screen-space delta (inverse of
/// screenDeltaToIsoDelta).
constexpr vec2 isoDeltaToScreenDelta(
    const vec2 isoDelta, const vec2 triangleStepSizeScreen
) {
    return isoDelta * triangleStepSizeScreen * vec2(1.0f, IRPlatform::kGfx.screenYDirection_);
}

/// Converts a screen-pixel offset to iso-triangle units.
constexpr vec2
offsetScreenToIsoTriangles(const vec2 offsetScreen, const vec2 triangleStepSizeScreen) {
    return offsetScreen / triangleStepSizeScreen;
}

/// Rounds each component of a vec2 symmetrically (away from zero for 0.5).
vec2 symmetricRound(const vec2 &input);

/// Returns the isometric depth scalar for @p position.
/// depth = x + y + z (higher values are further from the camera).
/// **Never inline this equation** — always call this helper.
constexpr Distance pos3DtoDistance(const ivec3 position) {
    return sumVecComponents(position);
}
/// @overload Floating-point position variant; rounds the result.
constexpr Distance pos3DtoDistance(const vec3 position) {
    return round(sumVecComponents(position));
}

/// Determines which isometric face (X, Y, Z, or NONE) a canvas triangle
/// index belongs to for a rectangular prism of @p size dimensions.
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

/// Inverse iso projection: maps a 2D iso canvas position back to a 3D
/// surface point on a rectangular prism of @p size dimensions.
/// Returns (-1, -1, -1) if the position does not hit any visible face.
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

/// Converts an iso 2D pixel offset to a game-resolution pixel offset.
/// Each iso pixel spans 2 game pixels horizontally and 1 vertically.
/// Used by the trixel-to-framebuffer stage for sub-pixel camera smoothing.
constexpr vec2 pos2DIsoToPos2DGameResolution(const vec2 position, const vec2 zoomLevel) {
    return position * zoomLevel * vec2(2, 1);
}

/// Inverse iso projection selecting the 3D position at a specific Z level,
/// reading from the bottom Z face.
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

/// Variant of pos2DIsoToPos3DAtZLevel that accepts a pre-adjusted position
/// relative to the canvas origin (no size template required).
constexpr ivec3 pos2DIsoToPos3DAtZLevelNew(const ivec2 positionFromOrigin, const int zLevel) {
    ivec2 positionZLevelAdjusted = positionFromOrigin - ivec2(0, zLevel * 2);
    return ivec3(
        glm::ceil(-(positionZLevelAdjusted.x + positionZLevelAdjusted.y) / 2.0),
        (positionZLevelAdjusted.x - positionZLevelAdjusted.y) / 2,
        zLevel
    );
}

/// Variant assuming the caller has already adjusted @p position to the
/// origin of the bottom Z face.  The two variants above wrap this with
/// different origin computations.
constexpr ivec3 pos2DIsoToPos3DAtZLevelAlt(const ivec2 position, const int zLevel) {
    return ivec3(
        glm::ceil(-(position.x + position.y) / 2.0),
        (position.x - position.y) / 2,
        zLevel
    );
}

/// Linearises a 2D index into a 1D array index (row-major order).
constexpr int index2DtoIndex1D(const ivec2 index, const ivec2 size) {
    return index.y * size.x + index.x;
}

/// Linearises a 3D index into a 1D array index (z-major, row-major order).
constexpr int index3DtoIndex1D(const ivec3 index, const ivec3 size) {
    return index.z * size.x * size.y + index.y * size.x + index.x;
}

// ISOMETRIC THINGS

/// Returns the "partner" canvas triangle for the given face type and index.
/// Trixels are rendered as pairs; each triangle in the pair maps to the same
/// voxel face at the opposite parity.
template <FaceType faceType> constexpr ivec2 calculatePartnerTriangleIndex(ivec2 index);

/// @overload X-face specialisation.
template <> constexpr ivec2 calculatePartnerTriangleIndex<FaceType::X_FACE>(ivec2 index) {
    if (IRMath::sumVecComponents(index) % 2 == 0) {
        return index + ivec2(0, -1);
    }

    return index + ivec2(0, 1);
}

/// @overload Y-face specialisation.
template <> constexpr ivec2 calculatePartnerTriangleIndex<FaceType::Y_FACE>(ivec2 index) {
    if (IRMath::sumVecComponents(index) % 2 == 0) {
        return index + ivec2(0, 1);
    }

    return index + ivec2(0, -1);
}

/// @overload Z-face specialisation.
template <> constexpr ivec2 calculatePartnerTriangleIndex<FaceType::Z_FACE>(ivec2 index) {
    if (IRMath::sumVecComponents(index) % 2 == 0) {
        return index + ivec2(-1, 0);
    }

    return index + ivec2(1, 0);
}

/// Returns the 2D trixel-canvas size (in iso pixels) required to hold a
/// 3D voxel region of @p size dimensions.
constexpr ivec2 size3DtoSize2DIso(const ivec3 size) {
    return ivec2(
        size.x + size.y,
        (size.x + size.y) + (size.z * 2) - 1 // TODO: check this
    );
}

/// Converts game-resolution pixel dimensions to iso-canvas pixel dimensions.
/// Each iso pixel spans 2 screen pixels horizontally and 1 vertically.
constexpr uvec2 gameResolutionToSize2DIso(const uvec2 gameResolution, const uvec2 scaleFactor) {
    return gameResolution / uvec2(2, 1) / scaleFactor;
}

/// @overload Floating-point variant with an optional scale factor.
constexpr vec2
gameResolutionToSize2DIso(const vec2 gameResolution, const vec2 scaleFactor = vec2(1.0f)) {
    return gameResolution / vec2(2, 1) / scaleFactor;
}

/// Returns the screen-pixel size of one iso triangle at the given zoom and
/// pixel scale.  Used by the trixel-to-framebuffer stage to map iso offsets
/// to screen offsets.
constexpr ivec2 calcTriangleStepSizeScreen(
    const vec2 gameResolution, const vec2 zoomLevel, const ivec2 pixelScaleFactor
) {
    return (
        ivec2(gameResolution / gameResolutionToSize2DIso(gameResolution, zoomLevel)) *
        pixelScaleFactor
    );
}

/// Same as calcTriangleStepSizeScreen with pixelScaleFactor = (1, 1).
constexpr ivec2
calcTriangleStepSizeGameResolution(const vec2 gameResolution, const vec2 zoomLevel) {
    return calcTriangleStepSizeScreen(gameResolution, zoomLevel, ivec2(1));
}

/// Converts iso-canvas pixel dimensions to game-resolution pixel dimensions
/// (floor division; marked UNTESTED).
constexpr uvec2 size2DIsoToGameResolution(const uvec2 size, const uvec2 scaleFactor) {
    // Floor division (THIS IS UNTESTED)
    return size / uvec2(1, 2) * scaleFactor;
}

/// Maps a fractional iso position to a trixel canvas index.
/// @p originModifier adjusts for parity alignment within the canvas.
vec2 pos2DIsoToTriangleIndex(const vec2 position, const int originModifier);

/// Fractional part of a float (free-function overload; always in [0, 1)).
float fract(float value);

/// Converts a float HSV colour (h ∈ [0,360], s/v ∈ [0,1]) to a float RGB
/// colour (all components ∈ [0,1]).
vec3 hsvToRgb(const vec3 &colorHSV);

/// Converts a float HSV colour to an 8-bit RGB byte triplet.
u8vec3 hsvToRgbBytes(const vec3 &colorHSV);

/// Returns the screen width in pixels for a given height and aspect ratio.
constexpr int
calcResolutionWidthFromHeightAndAspectRatio(const int height, const ivec2 aspectRatio) {
    return static_cast<int>(height * static_cast<float>(aspectRatio.y) / aspectRatio.x);
}

/// Returns the screen height in pixels for a given width and aspect ratio.
constexpr int
calcResolutionHeightFromWidthAndAspectRatio(const int width, const ivec2 aspectRatio) {
    return static_cast<int>(width * static_cast<float>(aspectRatio.x) / aspectRatio.y);
}

/// Converts a normalised float value [0, 1] to a uint8 byte [0, 255],
/// clamping to prevent overflow.
constexpr uint8_t roundFloatToByte(const float value) {
    const float clamped = clamp(value, 0.0f, 1.0f);
    return static_cast<uint8_t>(round(clamped * 255.0f));
}

/// Converts a uint8 byte [0, 255] to a normalised float [0, 1].
constexpr float roundByteToFloat(const uint8_t value) {
    return (float)value / 255.0f;
}

/// Linearly interpolates between @p from and @p to at parameter @p t
/// (clamped to [0, 1]) in byte space.
constexpr uint8_t lerpByte(uint8_t from, uint8_t to, float t) {
    const float tClamped = clamp(t, 0.0f, 1.0f);
    const float fromFloat = roundByteToFloat(from);
    const float toFloat = roundByteToFloat(to);
    return roundFloatToByte(mix(fromFloat, toFloat, tClamped));
}

/// Linearly interpolates each RGBA channel of @p from toward @p to at
/// parameter @p t (clamped to [0, 1]).
constexpr Color lerpColor(const Color &from, const Color &to, float t) {
    return Color{
        lerpByte(from.red_, to.red_, t),
        lerpByte(from.green_, to.green_, t),
        lerpByte(from.blue_, to.blue_, t),
        lerpByte(from.alpha_, to.alpha_, t)
    };
}

/// Linearly interpolates each HSV channel of @p from toward @p to at
/// parameter @p t (clamped to [0, 1]).
constexpr ColorHSV lerpHSV(const ColorHSV &from, const ColorHSV &to, float t) {
    const float tClamped = clamp(t, 0.0f, 1.0f);
    return ColorHSV{
        from.hue_ + (to.hue_ - from.hue_) * tClamped,
        from.saturation_ + (to.saturation_ - from.saturation_) * tClamped,
        from.value_ + (to.value_ - from.value_) * tClamped,
        from.alpha_ + (to.alpha_ - from.alpha_) * tClamped
    };
}

/// Rounds each component of @p value to the nearest integer.
constexpr ivec3 roundVec3ToIVec3(vec3 value) {
    return ivec3(round(value.x), round(value.y), round(value.z));
}

/// Canvas origin offsets per voxel face parity.
/// These map the world origin (0,0,0) to a canvas pixel near the centre of
/// the trixel canvas texture.  Each face type has a different sub-pixel
/// alignment because of how the isometric trixel grid is laid out.
/// The offset is added to floor(cameraIso) and the iso position to obtain
/// the final canvas pixel coordinate.
/// @{
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
/// @}

/// Converts a ColorHSV (float hue [0,360], saturation/value [0,1]) to a
/// packed RGBA Color.
Color colorHSVToColor(const ColorHSV &colorHSV);

/// Converts a packed RGBA Color to a ColorHSV (float hue/saturation/value).
ColorHSV colorToColorHSV(const Color &color);

/// Shifts the hue, saturation, and value of @p base by the amounts in
/// @p offset (which is interpreted as an HSV delta, not a full colour).
Color applyHSVOffset(const Color &base, const ColorHSV &offset);

/// Layout helpers for scriptable entity placement.
/// Each function returns the world position for entity @p index in a group of
/// @p count entities.  Call in a loop from 0 to count - 1.
///
/// @p plane controls which isometric plane the layout occupies:
///   - PlaneIso::XY — depth along Z
///   - PlaneIso::XZ — depth along Y
///   - PlaneIso::YZ — depth along X
/// @{

/// Rectangular grid layout, centred on the world origin.
vec3 layoutGridCentered(
    int index,
    int count,
    int columns,
    float spacingPrimary,
    float spacingSecondary,
    PlaneIso plane = PlaneIso::XY,
    float depth = 0.0f
);

/// Zig-zag layout centred on the world origin.
vec3 layoutZigZagCentered(
    int index,
    int count,
    int itemsPerZag,
    float spacingPrimary,
    float spacingSecondary,
    PlaneIso plane = PlaneIso::XY,
    float depth = 0.0f
);

/// Zig-zag path layout (entities follow a zig-zag trajectory).
vec3 layoutZigZagPath(
    int index,
    int count,
    int itemsPerSegment,
    float spacingPrimary,
    float spacingSecondary,
    PlaneIso plane = PlaneIso::XY,
    float depth = 0.0f
);

/// Outward square-spiral layout.
vec3 layoutSquareSpiral(
    int index,
    float spacing,
    PlaneIso plane = PlaneIso::XY,
    float depth = 0.0f
);

/// Circular ring layout.  @p startAngleRad is the angle of the first entity
/// (default: -π/2, placing entity 0 at the top).
vec3 layoutCircle(
    int index,
    int count,
    float radius,
    float startAngleRad = -1.57079633f,
    PlaneIso plane = PlaneIso::XY,
    float depth = 0.0f
);

/// Helical spiral layout.  @p turns is the total number of full rotations;
/// @p heightSpan is the total rise along @p axis.
vec3 layoutHelix(
    int index,
    int count,
    float radius,
    float turns,
    float heightSpan,
    CoordinateAxis axis = CoordinateAxis::ZAxis
);

/// Layout along a parametric arc path with tangent arcs between segments.
vec3 layoutPathTangentArcs(
    int index,
    int count,
    float radius,
    int blocksPerArc,
    float zStep,
    CoordinateAxis axis = CoordinateAxis::ZAxis,
    float startAngleRad = 0.785398163f,
    bool invert = false
);

/// @}

// 2D

/// i-hat basis vector: maps a 2D grid unit to iso screen space (half-tile offset).
template <vec2 objSize> constexpr vec2 kIHatGridToScreenIso = vec2(1.0f, 0.5f) * (objSize / 2.0f);

/// j-hat basis vector: maps a 2D grid unit to iso screen space (half-tile offset).
template <vec2 objSize> constexpr vec2 kJHatGridToScreenIso = vec2(-1.0f, 0.5f) * (objSize / 2.0f);

/// 2×2 transformation matrix from 2D grid space to iso screen space,
/// constructed from the i-hat and j-hat basis vectors.
template <vec2 iHat, vec2 jHat>
constexpr mat2 k2DGridToScreenIsoTransform =
    // TODO: is this order correct??
    mat2(iHat.x, jHat.x, iHat.y, jHat.y);

// 3D

/// Ordered step sequences for isometric raymarching traversal.
/// Each array defines the per-step axis increments for one face type and
/// parity (lower/upper, left/right).  Used by the voxel-to-trixel pipeline
/// to march rays across faces in iso order.
/// @{
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
/// @}

/// Converts a duration in seconds to a frame count at the given FPS.
template <int FPS> constexpr int secondsToFrames(float seconds) {
    return ceil(seconds * FPS);
}

} // namespace IRMath

#endif /* IR_MATH_H */
