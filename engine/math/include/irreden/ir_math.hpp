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

/// Loads a color palette from a binary or text file.
std::vector<Color> createColorPaletteFromFile(const char *filename);

/// Returns a random bool (50/50 chance).
const bool randomBool();
/// Returns a random int in the closed interval [min, max].
const int randomInt(const int min, const int max);
/// Returns a random float in the closed interval [min, max].
const float randomFloat(const float min, const float max);
/// Returns a random RGBA color with fully-opaque alpha (255).
const Color randomColor();
/// Returns a random color drawn uniformly from `colorPalette`.
const Color randomColor(const std::vector<Color> &colorPalette);

/// Returns a random vec3 with each component drawn from [min[i], max[i]].
const vec3 randomVec(const vec3 min, const vec3 max);

/// Rounds a float to the nearest integer (ties round away from zero).
constexpr int round(float value) {
    return glm::round(value);
}

/// Integer ceiling division: ⌈numerator / denominator⌉ using only integer ops.
constexpr int divCeil(int numerator, int denominator) {
    return (numerator + denominator - 1) / denominator;
}

/// Rounds each component of a vec2 to the nearest integer, returning an ivec2.
constexpr ivec2 roundVec(vec2 value) {
    return ivec2(round(value.x), round(value.y));
}

/// Component-wise maximum of two vectors (GLM max).
template <typename VecType> constexpr VecType max(const VecType &vector1, const VecType &vector2) {
    return glm::max(vector1, vector2);
}

/// Returns a unit-length vector in the same direction (GLM normalize).
template <typename VecType> constexpr VecType normalize(const VecType &vector) {
    return glm::normalize(vector);
}

/// Returns the Euclidean length of a vector (GLM length).
template <typename VecType> constexpr auto length(const VecType &vector) {
    return glm::length(vector);
}

/// Component-wise minimum (GLM min).
template <typename T> constexpr T min(const T &value1, const T &value2) {
    return glm::min(value1, value2);
}

/// Absolute value, component-wise for vectors (GLM abs).
template <typename T> constexpr T abs(const T &value) {
    return glm::abs(value);
}

/// Clamps value to [minValue, maxValue], component-wise for vectors (GLM clamp).
template <typename T> constexpr T clamp(const T &value, const T &minValue, const T &maxValue) {
    return glm::clamp(value, minValue, maxValue);
}

/// Linear interpolation: `value1 * (1 - t) + value2 * t` (GLM mix).
template <typename T, typename U> constexpr T mix(const T &value1, const T &value2, const U &t) {
    return glm::mix(value1, value2, t);
}

/// Dot product of two vectors (GLM dot).
template <typename T> constexpr auto dot(const T &value1, const T &value2) {
    return glm::dot(value1, value2);
}

/// Component-wise less-than comparison, returns bvec (GLM lessThan).
template <typename T> constexpr auto lessThan(const T &value1, const T &value2) {
    return glm::lessThan(value1, value2);
}

/// Component-wise greater-than-or-equal comparison (GLM greaterThanEqual).
template <typename T> constexpr auto greaterThanEqual(const T &value1, const T &value2) {
    return glm::greaterThanEqual(value1, value2);
}

/// Returns true if all components of a bvec are true (GLM all).
template <typename T> constexpr bool all(const T &value) {
    return glm::all(value);
}

/// Component-wise floor, returning the same vector type (GLM floor).
template <typename T> constexpr auto floor(const T &value) {
    return glm::floor(value);
}

/// Component-wise ceil, returning the same vector type (GLM ceil).
template <typename T> constexpr auto ceil(const T &value) {
    return glm::ceil(value);
}

/// Component-wise fractional part; result is in [0, 1) (GLM fract).
template <typename T> constexpr auto fract(const T &value) {
    return glm::fract(value);
}

/// Fractional part of the absolute value; always in [0, 1). Sign is ignored.
template <typename T> constexpr auto fractAbs(const T &value) {
    return glm::fract(glm::abs(value));
}

/// Sine of a float in radians (GLM sin).
constexpr float sin(float value) {
    return glm::sin(value);
}

/// Builds an orthographic projection matrix using the depth range of the
/// active backend: [0, 1] for Metal/Vulkan (`glm::orthoZO`), [-1, 1] for
/// OpenGL (`glm::orthoNO`). Dispatched at compile time via `kGfx`.
inline mat4 ortho(float left, float right, float bottom, float top, float nearZ, float farZ) {
    if constexpr (IRPlatform::kGfx.ndcDepthZeroToOne_) {
        return glm::orthoZO(left, right, bottom, top, nearZ, farZ);
    } else {
        return glm::orthoNO(left, right, bottom, top, nearZ, farZ);
    }
}

/// Returns a translation matrix (GLM translate).
inline mat4 translate(const mat4 &matrix, const vec3 &position) {
    return glm::translate(matrix, position);
}

/// Returns a scale matrix (GLM scale).
inline mat4 scale(const mat4 &matrix, const vec3 &value) {
    return glm::scale(matrix, value);
}

/// Returns the sum of the x and y components of an ivec2.
constexpr int sumVecComponents(const ivec2 value) {
    return value.x + value.y;
}

/// Returns the sum of the x, y, and z components of an ivec3.
constexpr int sumVecComponents(const ivec3 value) {
    return value.x + value.y + value.z;
}

/// Returns the sum of the x, y, and z components of a vec3.
/// Return type is int; the float sum is implicitly narrowed without rounding.
constexpr int sumVecComponents(const vec3 value) {
    return value.x + value.y + value.z;
}

/// Returns the sum of the x and y components of a vec2.
/// Return type is int; the float sum is implicitly narrowed without rounding.
constexpr int sumVecComponents(const vec2 value) {
    return value.x + value.y;
}

/// Returns the product of all three components of an ivec3.
constexpr int multVecComponents(const ivec3 value) {
    return value.x * value.y * value.z;
}

/// Iso-space 2D offset to the X=0 face origin of a 3D box of the given size.
/// Used by inverse-projection helpers to locate the reference corner.
constexpr ivec2 size3DtoOriginOffset2DX1(const uvec3 size) {
    return ivec2(size.x, size.x + size.y - 1);
}
/// Iso-space 2D offset to the Y=0 face origin (one column left of X=0).
constexpr ivec2 size3DtoOriginOffset2DY1(const uvec3 size) {
    return size3DtoOriginOffset2DX1(size) - ivec2(1, 0);
}
/// Iso-space 2D offset to the Z=0 face origin (one column left and one row down).
constexpr ivec2 size3DtoOriginOffset2DZ1(const uvec3 size) {
    return size3DtoOriginOffset2DX1(size) - ivec2(1, 1);
}

/// World 3D → iso 2D projection (ivec3 → ivec2).
/// Iso 2D is the coordinate space used for canvas pixel addressing:
///   canvasPixel = canvasOriginOffset + floor(cameraIso) + pos3DtoPos2DIso(world)
/// Formula: iso.x = -x + y, iso.y = -x - y + 2z.
/// Iso Y increases upward. Renderer-specific canvas-to-screen mapping
/// is handled outside these projection helpers.
constexpr ivec2 pos3DtoPos2DIso(const ivec3 position) {
    return ivec2(-position.x + position.y, -position.x - position.y + (2 * position.z));
}

/// World 3D → iso 2D projection (vec3 → vec2, float precision).
/// Same formula as the ivec3 overload.
constexpr vec2 pos3DtoPos2DIso(const vec3 position) {
    return vec2(-position.x + position.y, -position.x - position.y + (2 * position.z));
}

/// World 3D → screen 2D position.
/// Applies the iso projection and then maps to screen pixels using
/// `triangleStepSizeScreen`. The Y component is flipped by
/// `IRPlatform::kGfx.screenYDirection_` to match the active backend's
/// Y convention (OpenGL Y-up vs Metal/Vulkan Y-down).
constexpr vec2 pos3DtoPos2DScreen(const vec3 position, const vec2 triangleStepSizeScreen) {
    return pos3DtoPos2DIso(position) * triangleStepSizeScreen *
           vec2(-1.0f, IRPlatform::kGfx.screenYDirection_);
}

/// Shifts a 3D position along the isometric depth axis (1, 1, 1).
/// The returned position projects to the same 2D iso location but sits at a
/// different depth. Positive depth increases `pos3DtoDistance` (x+y+z), which
/// sorts the voxel further back in the distance buffer; negative depth moves
/// it in front.
constexpr vec3 isoDepthShift(const vec3 &position, float depth) {
    return position + vec3(depth);
}

/// Axis-aligned bounding box in iso 2D space (canvas pixel coordinates).
struct IsoBounds2D {
    vec2 min_;
    vec2 max_;

    /// Constructs the AABB from any two opposite corners (order-independent).
    static IsoBounds2D fromCorners(vec2 cornerA, vec2 cornerB) {
        return {
            vec2(glm::min(cornerA.x, cornerB.x), glm::min(cornerA.y, cornerB.y)),
            vec2(glm::max(cornerA.x, cornerB.x), glm::max(cornerA.y, cornerB.y))
        };
    }

    /// Returns true if `point` lies within or on the boundary of this AABB.
    bool contains(vec2 point) const {
        return point.x >= min_.x && point.x <= max_.x &&
               point.y >= min_.y && point.y <= max_.y;
    }

    /// Returns the center of the AABB.
    vec2 center() const { return (min_ + max_) * 0.5f; }
    /// Returns the full extent (width, height) of the AABB.
    vec2 extent() const { return max_ - min_; }
};

/// Computes the visible iso-space viewport given camera, canvas, and zoom.
/// This inverts the canvas-pixel formula:
///   canvasPixel = canvasOriginOffset + floor(cameraIso) + isoPos
/// At zoom Z, only the center canvasSize/Z of the canvas is on screen
/// (the trixel-to-framebuffer model matrix scales by resolution*zoom,
/// so pixels outside the center 1/Z fraction are off-screen).
/// Returns the iso-space min/max with an optional margin.
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

/// Conservative iso-space half-extent for a rectangular prism of `voxelSize`.
/// Cheaper than the exact 8-corner `entityIsoBounds` enumeration but
/// slightly overestimates due to per-axis ceil + 1 padding.
inline vec2 shapeIsoHalfExtent(vec3 voxelSize) {
    vec3 halfSize = voxelSize * 0.5f;
    float extentX = glm::ceil(halfSize.x) + 1.0f;
    float extentY = glm::ceil(halfSize.y) + 1.0f;
    float extentZ = glm::ceil(halfSize.z) + 1.0f;
    return vec2(extentX + extentY, extentX + extentY + 2.0f * extentZ);
}

/// Returns the exact iso-space AABB of an entity by projecting all 8 corners
/// of its voxel AABB (worldPos + [0,voxelSize]) to iso 2D and taking the
/// component-wise min/max.
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

/// Tests whether an entity's world AABB overlaps the trixel canvas.
/// Converts the world AABB to iso, then to canvas pixels, and tests
/// against [0, canvasSize). Operates entirely in canvas space.
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

// constexpr vec2 pos3DtoPos2DScreenOffset(const vec3 position) {
//     return pos3DtoPos2DIso(position) ...
// }

/// Converts a screen-center-relative position to iso space.
/// `triangleStepSizeScreen` is the screen pixel size of one iso unit.
constexpr vec2 pos2DScreenToPos2DIso(const vec2 screenPos, const vec2 triangleStepSizeScreen) {
    return screenPos / triangleStepSizeScreen;
}

/// Converts a screen-space delta to an iso-space delta.
/// The Y component is scaled by `IRPlatform::kGfx.screenYDirection_` to
/// account for the backend's Y-axis orientation (OpenGL vs Metal/Vulkan).
constexpr vec2 screenDeltaToIsoDelta(
    const vec2 screenDelta, const vec2 triangleStepSizeScreen
) {
    return screenDelta / triangleStepSizeScreen * vec2(1.0f, IRPlatform::kGfx.screenYDirection_);
}

/// Converts an iso-space delta to a screen-space delta (inverse of `screenDeltaToIsoDelta`).
constexpr vec2 isoDeltaToScreenDelta(
    const vec2 isoDelta, const vec2 triangleStepSizeScreen
) {
    return isoDelta * triangleStepSizeScreen * vec2(1.0f, IRPlatform::kGfx.screenYDirection_);
}

/// Converts a screen-space offset to iso triangle units.
/// Equivalent to `pos2DScreenToPos2DIso` but named for the offset context.
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

/// Rounds an iso 2D position to the nearest half-integer triangle boundary.
vec2 symmetricRound(const vec2 &input);

/// Computes the trixel depth value for a 3D position: depth = x + y + z.
/// Used as the distance buffer value to determine voxel draw order.
constexpr Distance pos3DtoDistance(const ivec3 position) {
    return sumVecComponents(position);
}
/// Float-precision variant; rounds the summed components to the nearest int.
constexpr Distance pos3DtoDistance(const vec3 position) {
    return round(sumVecComponents(position));
}

/// Determines which isometric face (X, Y, Z, or NONE) a canvas triangle index
/// belongs to for a voxel chunk of the given size.
/// Template parameter `size` is the chunk dimensions in voxels.
/// The origin offset is computed via `size3DtoOriginOffset2DX1` to locate
/// the reference corner, then positional tests select the face.
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

/// Maps an iso 2D canvas position to the 3D surface voxel of a rectangular
/// prism (template size). Returns the voxel at the top-visible face, or
/// (-1, -1, -1) if `position` does not land on any face.
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

/// Converts an iso 2D offset to a game-resolution pixel offset.
/// Each iso X unit spans 2 screen pixels horizontally; each iso Y unit spans
/// 1 screen pixel vertically. Used by the trixel-to-framebuffer blit for
/// sub-pixel camera smoothing.
constexpr vec2 pos2DIsoToPos2DGameResolution(const vec2 position, const vec2 zoomLevel) {
    return position * zoomLevel * vec2(2, 1);
}

/// Maps an iso 2D canvas position to the 3D voxel at the given Z level,
/// selecting from the bottom Z face.
/// Template parameter `size` is the chunk dimensions — used to compute the
/// X=0 origin offset as the reference corner.
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

/// Variant of `pos2DIsoToPos3DAtZLevel` that takes a position already relative
/// to the chunk origin (no size template required). Applies the Z-level row
/// adjustment internally.
constexpr ivec3 pos2DIsoToPos3DAtZLevelNew(const ivec2 positionFromOrigin, const int zLevel) {
    ivec2 positionZLevelAdjusted = positionFromOrigin - ivec2(0, zLevel * 2);
    return ivec3(
        glm::ceil(-(positionZLevelAdjusted.x + positionZLevelAdjusted.y) / 2.0),
        (positionZLevelAdjusted.x - positionZLevelAdjusted.y) / 2,
        zLevel
    );
}

/// Variant assuming `position` has already been adjusted to the origin of the
/// bottom Z face. The two variants above wrap this with different origin computations.
constexpr ivec3 pos2DIsoToPos3DAtZLevelAlt(const ivec2 position, const int zLevel) {
    return ivec3(
        glm::ceil(-(position.x + position.y) / 2.0),
        (position.x - position.y) / 2,
        zLevel
    );
}

/// Converts a 2D grid index to a 1D array index using row-major order.
constexpr int index2DtoIndex1D(const ivec2 index, const ivec2 size) {
    return index.y * size.x + index.x;
}

/// Converts a 3D grid index to a 1D array index using Z-major, Y-row-major order.
constexpr int index3DtoIndex1D(const ivec3 index, const ivec3 size) {
    return index.z * size.x * size.y + index.y * size.x + index.x;
}

// ISOMETRIC THINGS

/// Returns the iso 2D canvas index of the triangle that forms the other half
/// of the same voxel face as `index`. Template-specialized per face type.
template <FaceType faceType> constexpr ivec2 calculatePartnerTriangleIndex(ivec2 index);

/// X-face partner: parity determined by sum(index) % 2; shifts one row up or down.
template <> constexpr ivec2 calculatePartnerTriangleIndex<FaceType::X_FACE>(ivec2 index) {
    if (IRMath::sumVecComponents(index) % 2 == 0) {
        return index + ivec2(0, -1);
    }

    return index + ivec2(0, 1);
}

/// Y-face partner: same parity logic as X-face but inverted row direction.
template <> constexpr ivec2 calculatePartnerTriangleIndex<FaceType::Y_FACE>(ivec2 index) {
    if (IRMath::sumVecComponents(index) % 2 == 0) {
        return index + ivec2(0, 1);
    }

    return index + ivec2(0, -1);
}

/// Z-face partner: parity shifts one column left or right instead of a row.
template <> constexpr ivec2 calculatePartnerTriangleIndex<FaceType::Z_FACE>(ivec2 index) {
    if (IRMath::sumVecComponents(index) % 2 == 0) {
        return index + ivec2(-1, 0);
    }

    return index + ivec2(1, 0);
}

/// Returns the iso 2D canvas dimensions (in triangles) needed to display a
/// 3D chunk of the given size at 1:1 zoom.
/// Width  = size.x + size.y
/// Height = size.x + size.y + 2*size.z - 1
constexpr ivec2 size3DtoSize2DIso(const ivec3 size) {
    return ivec2(
        size.x + size.y,
        (size.x + size.y) + (size.z * 2) - 1 // TODO: check this
    );
}

/// Converts a game resolution (screen pixels) to a canvas size (iso pixels).
/// Each iso pixel spans 2 screen pixels horizontally and 1 vertically.
/// `scaleFactor` is an additional integer downscale applied after the 2:1 conversion.
constexpr uvec2 gameResolutionToSize2DIso(const uvec2 gameResolution, const uvec2 scaleFactor) {
    return gameResolution / uvec2(2, 1) / scaleFactor;
}

/// Float-precision overload of `gameResolutionToSize2DIso`.
/// `scaleFactor` defaults to (1, 1) if omitted.
constexpr vec2
gameResolutionToSize2DIso(const vec2 gameResolution, const vec2 scaleFactor = vec2(1.0f)) {
    return gameResolution / vec2(2, 1) / scaleFactor;
}

/// Returns the screen pixel size of one iso triangle at the given zoom level
/// and pixel scale factor, rounded to the nearest integer.
constexpr ivec2 calcTriangleStepSizeScreen(
    const vec2 gameResolution, const vec2 zoomLevel, const ivec2 pixelScaleFactor
) {
    return (
        ivec2(gameResolution / gameResolutionToSize2DIso(gameResolution, zoomLevel)) *
        pixelScaleFactor
    );
}

/// Returns the game-resolution (non-screen-scaled) pixel size of one iso
/// triangle at the given zoom level (`pixelScaleFactor` = 1).
constexpr ivec2
calcTriangleStepSizeGameResolution(const vec2 gameResolution, const vec2 zoomLevel) {
    return calcTriangleStepSizeScreen(gameResolution, zoomLevel, ivec2(1));
}

/// Converts an iso canvas size to a game resolution.
/// Inverse of `gameResolutionToSize2DIso` (integer, floor division — UNTESTED).
constexpr uvec2 size2DIsoToGameResolution(const uvec2 size, const uvec2 scaleFactor) {
    // Floor division (THIS IS UNTESTED)
    return size / uvec2(1, 2) * scaleFactor;
}

/// Maps an iso 2D position to the sub-triangle grid index it falls in.
/// `originModifier` shifts the parity baseline.
vec2 pos2DIsoToTriangleIndex(const vec2 position, const int originModifier);

/// Scalar fractional part for a single float (overloads the template version above).
float fract(float value);

/// Converts HSV (hue 0–360, saturation 0–1, value 0–1) to RGB (each 0–1).
vec3 hsvToRgb(const vec3 &colorHSV);

/// Converts HSV to RGB and returns each channel as a uint8_t byte (0–255).
u8vec3 hsvToRgbBytes(const vec3 &colorHSV);

/// Returns the display width in pixels for a given height and aspect ratio.
/// Convention: `aspectRatio.x` is the height portion, `aspectRatio.y` is the
/// width portion — e.g. `ivec2(9, 16)` for a 16:9 display.
constexpr int
calcResolutionWidthFromHeightAndAspectRatio(const int height, const ivec2 aspectRatio) {
    return static_cast<int>(height * static_cast<float>(aspectRatio.y) / aspectRatio.x);
}

/// Returns the display height in pixels for a given width and aspect ratio.
/// Same convention: `aspectRatio.x` = height portion, `aspectRatio.y` = width portion.
constexpr int
calcResolutionHeightFromWidthAndAspectRatio(const int width, const ivec2 aspectRatio) {
    return static_cast<int>(width * static_cast<float>(aspectRatio.x) / aspectRatio.y);
}

/// Clamps a float in [0, 1] and rounds it to the nearest uint8_t byte (0–255).
/// Clamping prevents uint8 overflow/wrap that would occur without it.
constexpr uint8_t roundFloatToByte(const float value) {
    const float clamped = clamp(value, 0.0f, 1.0f);
    return static_cast<uint8_t>(round(clamped * 255.0f));
}

/// Converts a uint8_t byte (0–255) to a normalized float in [0, 1].
constexpr float roundByteToFloat(const uint8_t value) {
    return (float)value / 255.0f;
}

/// Linearly interpolates between two uint8_t byte values using a float `t` in
/// [0, 1] (clamped). Converts to float, interpolates, then rounds back.
constexpr uint8_t lerpByte(uint8_t from, uint8_t to, float t) {
    const float tClamped = clamp(t, 0.0f, 1.0f);
    const float fromFloat = roundByteToFloat(from);
    const float toFloat = roundByteToFloat(to);
    return roundFloatToByte(mix(fromFloat, toFloat, tClamped));
}

/// Linearly interpolates between two RGBA Colors component-wise.
/// Each channel is interpolated via `lerpByte`; `t` is clamped to [0, 1].
constexpr Color lerpColor(const Color &from, const Color &to, float t) {
    return Color{
        lerpByte(from.red_, to.red_, t),
        lerpByte(from.green_, to.green_, t),
        lerpByte(from.blue_, to.blue_, t),
        lerpByte(from.alpha_, to.alpha_, t)
    };
}

/// Linearly interpolates between two ColorHSV values component-wise.
/// `t` is clamped to [0, 1]; all four channels (H, S, V, A) are interpolated.
/// Does not account for hue wrapping — if hues differ by more than 180°,
/// consider a shortest-path hue interpolation instead.
constexpr ColorHSV lerpHSV(const ColorHSV &from, const ColorHSV &to, float t) {
    const float tClamped = clamp(t, 0.0f, 1.0f);
    return ColorHSV{
        from.hue_ + (to.hue_ - from.hue_) * tClamped,
        from.saturation_ + (to.saturation_ - from.saturation_) * tClamped,
        from.value_ + (to.value_ - from.value_) * tClamped,
        from.alpha_ + (to.alpha_ - from.alpha_) * tClamped
    };
}

/// Rounds each component of a vec3 to the nearest integer and returns an ivec3.
constexpr ivec3 roundVec3ToIVec3(vec3 value) {
    return ivec3(round(value.x), round(value.y), round(value.z));
}

/// Canvas origin offsets per isometric voxel face and sub-face index.
/// These map the world origin (0, 0, 0) to a canvas pixel near the center of
/// the trixel canvas texture. Each face type has a different sub-pixel
/// alignment because of how the isometric trixel grid is laid out.
/// The offset is added to floor(cameraIso) and the iso position to produce
/// the final canvas pixel coordinate.
constexpr ivec2 trixelOriginOffsetX1(const ivec2 &trixelCanvasSize) {
    return trixelCanvasSize / ivec2(2);
}
/// X-face, upper triangle — one row above X1.
constexpr ivec2 trixelOriginOffsetX2(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize) + ivec2(0, 1);
}
/// Y-face, lower triangle — one column left of X1.
constexpr ivec2 trixelOriginOffsetY1(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize) + ivec2(-1, 0);
}
/// Y-face, upper triangle — one column left and one row above X1.
constexpr ivec2 trixelOriginOffsetY2(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize) + ivec2(-1, 1);
}
/// Z-face, lower-left triangle — one column left and one row below X1.
constexpr ivec2 trixelOriginOffsetZ1(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize) + ivec2(-1, -1);
}
/// Z-face, lower-right triangle — one row below X1.
constexpr ivec2 trixelOriginOffsetZ2(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize) + ivec2(0, -1);
}
// back faces
/// X back-face, lower triangle (alias of Z1 offset).
constexpr ivec2 trixelOriginOffsetX3(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetZ1(trixelCanvasSize);
}
/// X back-face, upper triangle (alias of Y1 offset).
constexpr ivec2 trixelOriginOffsetX4(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetY1(trixelCanvasSize);
}
/// Y back-face, lower triangle (alias of Z2 offset).
constexpr ivec2 trixelOriginOffsetY3(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetZ2(trixelCanvasSize);
}
/// Y back-face, upper triangle (alias of X1 offset).
constexpr ivec2 trixelOriginOffsetY4(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize);
}
/// Z back-face, left triangle (alias of Y2 offset).
constexpr ivec2 trixelOriginOffsetZ3(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetY2(trixelCanvasSize);
}
/// Z back-face, right triangle (alias of X2 offset).
constexpr ivec2 trixelOriginOffsetZ4(const ivec2 &trixelCanvasSize) {
    return trixelOriginOffsetX2(trixelCanvasSize);
}

/// Converts a ColorHSV to an RGBA Color (via `hsvToRgb`).
Color colorHSVToColor(const ColorHSV &colorHSV);

/// Converts an RGBA Color to a ColorHSV.
ColorHSV colorToColorHSV(const Color &color);

/// Applies an HSV offset to an RGBA base color and returns the result.
/// Useful for palette shifts (hue rotation, saturation/value adjustments).
Color applyHSVOffset(const Color &base, const ColorHSV &offset);

/// Layout helpers for scriptable entity placement on isometric planes.
/// `plane` selects which axis carries the secondary depth:
///   PlaneIso::XY = depth along Z
///   PlaneIso::XZ = depth along Y
///   PlaneIso::YZ = depth along X
/// Returns the 3D world position for entity at `index` within the layout.

/// Places entities in a centered rectangular grid.
vec3 layoutGridCentered(
    int index,
    int count,
    int columns,
    float spacingPrimary,
    float spacingSecondary,
    PlaneIso plane = PlaneIso::XY,
    float depth = 0.0f
);

/// Places entities in a centered zig-zag pattern (alternating row offsets).
vec3 layoutZigZagCentered(
    int index,
    int count,
    int itemsPerZag,
    float spacingPrimary,
    float spacingSecondary,
    PlaneIso plane = PlaneIso::XY,
    float depth = 0.0f
);

/// Places entities along a zig-zag path (non-centered, follows a traversal).
vec3 layoutZigZagPath(
    int index,
    int count,
    int itemsPerSegment,
    float spacingPrimary,
    float spacingSecondary,
    PlaneIso plane = PlaneIso::XY,
    float depth = 0.0f
);

/// Places entities along an outward square spiral with uniform spacing.
vec3 layoutSquareSpiral(
    int index,
    float spacing,
    PlaneIso plane = PlaneIso::XY,
    float depth = 0.0f
);

/// Places entities evenly around a circle of the given radius.
/// `startAngleRad` defaults to -π/2 (top of circle).
vec3 layoutCircle(
    int index,
    int count,
    float radius,
    float startAngleRad = -1.57079633f,
    PlaneIso plane = PlaneIso::XY,
    float depth = 0.0f
);

/// Places entities along a helix with the given radius, number of turns,
/// and total height span. `axis` selects the helical axis (default Z).
vec3 layoutHelix(
    int index,
    int count,
    float radius,
    float turns,
    float heightSpan,
    CoordinateAxis axis = CoordinateAxis::ZAxis
);

/// Places entities along a path made of tangent circular arcs.
/// `blocksPerArc` controls how many entities fit in each arc segment;
/// `zStep` shifts each arc segment along the chosen axis.
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

// 2D

/// Iso-space step vector for the i-hat (column) direction of a 2D grid.
/// Maps one grid column unit to screen iso coordinates.
template <vec2 objSize> constexpr vec2 kIHatGridToScreenIso = vec2(1.0f, 0.5f) * (objSize / 2.0f);

/// Iso-space step vector for the j-hat (row) direction of a 2D grid.
/// Maps one grid row unit to screen iso coordinates (negative X, positive Y).
template <vec2 objSize> constexpr vec2 kJHatGridToScreenIso = vec2(-1.0f, 0.5f) * (objSize / 2.0f);

/// 2×2 transformation matrix from grid (i,j) space to iso screen space,
/// built from `iHat` and `jHat` column vectors.
template <vec2 iHat, vec2 jHat>
constexpr mat2 k2DGridToScreenIsoTransform =
    // TODO: is this order correct??
    mat2(iHat.x, jHat.x, iHat.y, jHat.y);

// 3D

/// Isometric voxel traversal step sequences.
/// Each array gives the 5-step DDA walk used when raymarching along a face.
/// The steps cycle, advancing through voxel neighbors in iso order.

// X = 0 face (left)

/// Raymarch step sequence for the lower triangle of the X face.
constexpr ivec3 kRaymarchStepsXFaceLower[] = {
    ivec3(0, 1, 0),
    ivec3(1, 0, 0),
    ivec3(0, 0, 1),
    ivec3(0, 1, 0),
    ivec3(1, 0, 0),
};

/// Raymarch step sequence for the upper triangle of the X face.
constexpr ivec3 kRaymarchStepXFaceUpper[] = {
    ivec3(0, 1, 0),
    ivec3(1, 0, 0),
    ivec3(0, 1, 0),
    ivec3(0, 0, 1),
    ivec3(1, 0, 0),
};

/// Raymarch step sequence for the upper triangle of the Y face.
constexpr ivec3 kRaymarchStepYFaceUpper[] = {
    ivec3(1, 0, 0),
    ivec3(0, 1, 0),
    ivec3(1, 0, 0),
    ivec3(0, 0, 1),
    ivec3(0, 1, 0),
};

/// Raymarch step sequence for the lower triangle of the Y face.
constexpr ivec3 kRaymarchStepsYFaceLower[] = {
    ivec3(1, 0, 0), ivec3(0, 1, 0), ivec3(0, 0, 1), ivec3(1, 0, 0), ivec3(0, 1, 0)
};

/// Raymarch step sequence for the left triangle of the Z face.
constexpr ivec3 kRaymarchStepsZFaceLeft[] = {
    ivec3(1, 0, 0),
    ivec3(0, 1, 0),
    ivec3(1, 0, 0),
    ivec3(0, 1, 0),
    ivec3(0, 0, 1),
};

/// Raymarch step sequence for the right triangle of the Z face.
constexpr ivec3 kRaymarchStepsZFaceRight[] = {
    ivec3(0, 1, 0),
    ivec3(1, 0, 0),
    ivec3(0, 1, 0),
    ivec3(1, 0, 0),
    ivec3(0, 0, 1),
};

/// Converts a duration in seconds to a frame count at the given FPS.
/// Rounds up so the action completes within the allotted time.
template <int FPS> constexpr int secondsToFrames(float seconds) {
    return ceil(seconds * FPS);
}

} // namespace IRMath

#endif /* IR_MATH_H */
