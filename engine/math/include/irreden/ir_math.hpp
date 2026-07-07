#ifndef IR_MATH_H
#define IR_MATH_H

#include <irreden/math/ir_math_types.hpp>
#include <irreden/math/easing_functions.hpp>
#include <irreden/math/color_palettes.hpp>
#include <irreden/math/color.hpp>
#include <irreden/math/physics.hpp>
#include <irreden/math/sdf.hpp>
#include <irreden/ir_platform.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

// TODO: The game engine needs transformations for voxel sets
// rotation in 3D space

namespace IRMath {

/// Per-thread `std::mt19937` used by every `IRMath::random*` call.
///
/// Each OS thread holds its own RNG state (`thread_local`), so concurrent
/// calls from IRJob workers never race. Threads default to a seed of `0`;
/// `IRJob::JobManager` calls @ref seedThreadRng on the main thread at
/// construction and on each worker the first time it enters a task body,
/// so worker N's RNG is deterministically seeded from `N`. Outside the
/// job system, callers can call @ref seedThreadRng directly (mirrors
/// `std::srand` but per-thread).
std::mt19937 &threadRng();

/// Seeds the calling thread's RNG (see @ref threadRng). Idempotent —
/// the most recent call wins.
void seedThreadRng(uint32_t seed);

/// Pi to float precision. Mirrors `glm::pi<float>()` but routes through the
/// IRMath wrapper so callers don't have to reach for `glm::` directly.
constexpr float kPi = 3.14159265358979323846f;
/// Half pi (π/2). Used by cardinal-yaw snapping (see `rasterYawCardinalIndex`).
constexpr float kHalfPi = 1.57079632679489661923f;
/// Two pi (2π). Useful for full-revolution math and angle wrap.
constexpr float kTwoPi = 6.28318530717958647692f;
/// Quarter pi (π/4). Cardinal-yaw residual bound, diagonal transforms.
constexpr float kQuarterPi = 0.78539816339744830961f;
/// √2. Horizontal in-plane growth factor of the Y/X-face deformation at the
/// residual-yaw bound ±π/4. The full stretched column is (√2, −1), length √3;
/// √2 is its horizontal component (see perAxisTrixelCanvasWorstCaseSize).
constexpr float kSqrt2 = 1.41421356237309504880f;

/// Face-index constants for the three visible iso faces. CPU mirror of the
/// `kXFace`/`kYFace`/`kZFace` integer constants in `shaders/ir_iso_common.glsl`
/// and `shaders/metal/ir_iso_common.metal`. Distinct from the legacy `FaceType`
/// enum in `ir_math_types.hpp` (which is 1-indexed with `NONE_FACE` first);
/// these match the shader convention so any helper that crosses the CPU↔GPU
/// boundary can use the same integer in both directions.
///
/// These three indices are AXIS-only (0 = X-axis face, 1 = Y-axis face,
/// 2 = Z-axis face), NOT polarity-aware: the X-axis face is whichever of
/// X_NEG / X_POS the camera is looking at. The six-face polarity-aware
/// FaceId enum below replaces this for visible-triplet rasterization
/// (#1278) — `kXFace` etc. are kept for the still-3-face helpers
/// (`faceDeformationMatrix`, AO tangent selection) since the deformation
/// matrix is identical for X_NEG / X_POS.
/// @{
constexpr int kXFace = 0;
constexpr int kYFace = 1;
constexpr int kZFace = 2;
/// @}

/// Polarity-aware six-face enumeration matching the model in
/// [`docs/design/voxel-face-rasterization.md`](../../../../docs/design/voxel-face-rasterization.md).
/// One enum value per cube face — explicitly NOT three axes with a ± sign.
/// Bit positions intentionally line up with the face-occlusion bits in
/// `IRComponents::VoxelFlags::kFaceOccluded*`: `bit(FaceId f) = 2 + int(f)`.
///
/// CPU mirror: shader-side constants `kFaceXNeg`/.../`kFaceZPos` in
/// `shaders/ir_iso_common.glsl` and `metal/ir_iso_common.metal`. CPU and
/// GPU MUST agree on the integer value of each face for the
/// `visibleFaceIds_` UBO handshake (`FrameDataVoxelToCanvas`).
enum class FaceId : int {
    X_NEG = 0,
    X_POS = 1,
    Y_NEG = 2,
    Y_POS = 3,
    Z_NEG = 4,
    Z_POS = 5,
    NONE = 6,
};

/// Axis index (0/1/2 = X/Y/Z) for a FaceId. The deformation matrix is
/// per-axis, not per-polarity, so use this when forwarding into the
/// 3-face `faceDeformationMatrix(int, float)`.
constexpr int faceAxis(FaceId f) {
    return static_cast<int>(f) >> 1; // {0,1}→0, {2,3}→1, {4,5}→2
}

/// True when @p f is the positive-coordinate face of its axis (X_POS,
/// Y_POS, Z_POS). The matching negative face is `bit(f) ^ 1`.
constexpr bool faceIsPositive(FaceId f) {
    return (static_cast<int>(f) & 1) != 0;
}

/// Bit position within `IRComponents::VoxelFlags::flags_` byte for the
/// occlusion bit of @p f. Bit set ⟺ neighbor exists ⟺ face occluded.
/// Inverting (`& ~kFaceOccludedMask` ^ 0x3F) at the shader yields the
/// exposed-face mask consumed by visible-triplet rasterization.
constexpr int faceOcclusionBit(FaceId f) {
    return 2 + static_cast<int>(f);
}

/// World-frame outward unit normal for @p f. Used by AO neighbor-step
/// math and Lambert shading. Six-face generalization of the
/// three-face `faceOutwardNormal(int)` shader helper, which only
/// returned the X_NEG / Y_NEG / Z_NEG normals.
constexpr vec3 faceOutwardNormal(FaceId f) {
    switch (f) {
    case FaceId::X_NEG:
        return vec3(-1.0f, 0.0f, 0.0f);
    case FaceId::X_POS:
        return vec3(1.0f, 0.0f, 0.0f);
    case FaceId::Y_NEG:
        return vec3(0.0f, -1.0f, 0.0f);
    case FaceId::Y_POS:
        return vec3(0.0f, 1.0f, 0.0f);
    case FaceId::Z_NEG:
        return vec3(0.0f, 0.0f, -1.0f);
    case FaceId::Z_POS:
        return vec3(0.0f, 0.0f, 1.0f);
    default:
        return vec3(0.0f);
    }
}

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

/// Round-half-up: rounds to the nearest integer, with ties going UP toward
/// +infinity. Implemented as `floor(x + 0.5)`.
///
/// This is the rounding rule we use for converting float voxel positions to
/// integer voxel cells whenever CPU and GPU need to agree byte-for-byte.
/// `glm::round`/`std::lround` use round-half-away-from-zero, which mismatches
/// GLSL's implementation-defined `round()` for negative half-integer values
/// (e.g. -0.5 → 0 with round-half-up vs -1 with round-half-away-from-zero).
/// Use this for occupancy-grid cell assignment, ray-march cell sampling, and
/// any other CPU↔GPU coordinate handshake.
///
/// GPU mirrors live as `roundHalfUp()` in `shaders/ir_iso_common.glsl` and
/// `shaders/metal/ir_iso_common.metal` and MUST stay byte-compatible.
constexpr int roundHalfUp(float value) {
    return static_cast<int>(glm::floor(value + 0.5f));
}

/// Component-wise round-half-up of a 3-vector. See @ref roundHalfUp.
constexpr ivec3 roundVec3HalfUp(vec3 value) {
    return ivec3(roundHalfUp(value.x), roundHalfUp(value.y), roundHalfUp(value.z));
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

/// Cross product of two 3-vectors. GLM wrapper.
template <typename VecType> constexpr VecType cross(const VecType &value1, const VecType &value2) {
    return glm::cross(value1, value2);
}

// Duff et al. 2017 — builds an orthonormal basis (outU, outV) perpendicular
// to unit vector n without branch discontinuities. The denominator (s + n.z)
// is always non-zero and numerically safe across the full n.z ∈ [-1, 1]
// range: copysign ensures it avoids the 0 singularity at n.z = 0.
inline void buildOrthonormalBasis(vec3 n, vec3 &outU, vec3 &outV) {
    const float s = std::copysign(1.0f, n.z);
    const float a = -1.0f / (s + n.z);
    const float b = n.x * n.y * a;
    outU = vec3(1.0f + s * n.x * n.x * a, s * b, -s * n.x);
    outV = vec3(b, s + n.y * n.y * a, -n.y);
}

// Hamilton product: in column-vector convention, rotates b first then a (bone hierarchy:
// quatMul(parent_world, local)).
inline vec4 quatMul(const vec4 &a, const vec4 &b) {
    return vec4(
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    );
}

// Rodrigues' formula: v + q.w*t + cross(q.xyz, t), t = 2*cross(q.xyz, v).
inline vec3 rotateVectorByQuat(const vec3 &v, const vec4 &q) {
    const vec3 u{q.x, q.y, q.z};
    const vec3 t = 2.0f * glm::cross(u, v);
    return v + q.w * t + glm::cross(u, t);
}

// Unit quaternion for a rotation of `angle` radians about a (possibly
// non-unit) axis. Layout: vec4(qx, qy, qz, qw) — same as C_LocalTransform.
inline vec4 quatAxisAngle(const vec3 &axis, float angle) {
    const vec3 a = normalize(axis);
    const float h = angle * 0.5f;
    return vec4(a.x * sin(h), a.y * sin(h), a.z * sin(h), cos(h));
}

// Inverse of a UNIT quaternion — conjugate (negate xyz, keep w). For
// non-unit input the result is the conjugate, not the true inverse;
// callers must pass unit quaternions (which is what every IRMath
// producer — `quatAxisAngle`, identity, `quatMul` of unit operands —
// returns).
inline vec4 quatInverse(const vec4 &q) {
    return vec4(-q.x, -q.y, -q.z, q.w);
}

// Z-axis Tait-Bryan yaw extracted from a unit quaternion. For a pure-Z
// rotation `q = quatAxisAngle(vec3(0,0,1), y)` this returns `y` exactly
// (round-trip identity). For a general SO(3) quaternion this returns the
// Z-component of the ZYX decomposition — the angle a pure-Z observer would
// attribute to the orientation. Result lies in (-π, π].
inline float quatExtractZAngle(const vec4 &q) {
    const float numer = 2.0f * (q.w * q.z + q.x * q.y);
    const float denom = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    return glm::atan(numer, denom);
}

/// Inverse of @ref pos3DtoPos2DIso: reconstructs the unique world position
/// at iso (x, y) on the depth plane @p depth (= x+y+z). The iso depth axis
/// is (1,1,1), so a 2D iso point and a depth value pin a single 3D point.
/// CPU mirror of `isoPixelToPos3D` in `shaders/ir_iso_common.glsl`.
constexpr vec3 isoPixelToPos3D(int isoX, int isoY, float depth) {
    const float fx = static_cast<float>(isoX);
    const float fy = static_cast<float>(isoY);
    const float wx = (2.0f * depth - 3.0f * fx - fy) / 6.0f;
    const float wy = wx + fx;
    const float wz = (fy + 2.0f * wx + fx) / 2.0f;
    return vec3(wx, wy, wz);
}

/// Float-precise overload of @ref isoPixelToPos3D taking a fractional iso
/// point (e.g. a sub-trixel camera pan). The int overload requires integer iso
/// coordinates; this overload accepts fractional values and preserves sub-pixel
/// precision, so the round-trip
/// `pos3DtoPos2DIso(isoPixelToPos3D(iso, depth))` is the exact identity for any
/// real @p iso — required when re-projecting a fractional offset under yaw must
/// collapse back to the original offset at `yaw == 0`.
constexpr vec3 isoPixelToPos3D(vec2 iso, float depth) {
    const float wx = (2.0f * depth - 3.0f * iso.x - iso.y) / 6.0f;
    const float wy = wx + iso.x;
    const float wz = (iso.y + 2.0f * wx + iso.x) / 2.0f;
    return vec3(wx, wy, wz);
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

/// Floating-point remainder: x - y * trunc(x / y). std::fmod equivalent.
inline float fmod(float x, float y) {
    return std::fmod(x, y);
}

/// Fractional part of the absolute value; always in [0, 1). Ignores sign.
template <typename T> constexpr auto fractAbs(const T &value) {
    return glm::fract(glm::abs(value));
}

/// Sine of @p value (radians). GLM wrapper.
constexpr float sin(float value) {
    return glm::sin(value);
}

/// Cosine of @p value (radians). GLM wrapper.
constexpr float cos(float value) {
    return glm::cos(value);
}

/// Square root of @p value. GLM wrapper.
constexpr float sqrt(float value) {
    return glm::sqrt(value);
}

/// Cube root of @p value (float). Wraps std::cbrt.
inline float cbrt(float value) noexcept {
    return std::cbrt(value);
}

/// Cube root of @p value (double). Wraps std::cbrt.
inline double cbrt(double value) noexcept {
    return std::cbrt(value);
}

/// Two-argument arctangent (radians). Result is in (-π, π]; sign matches
/// GLSL/std::atan2 convention. GLM wrapper.
inline float atan2(float y, float x) {
    return glm::atan(y, x);
}

/// 2D rotation of @p v by @p angle radians around the origin. The rotation
/// is CCW for positive angle in a Y-up coordinate system; the visible
/// direction reverses when applied in a Y-down system.
constexpr vec2 rotate2D(const vec2 v, float angle) {
    const float c = glm::cos(angle);
    const float s = glm::sin(angle);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

/// Strongly-typed index for the four camera-yaw cardinal snap points.
/// Mirrors the 0..3 range used by `rasterYawCardinalIndex` in
/// `shaders/ir_iso_common.glsl`. CPU and GPU MUST agree on the same index for
/// any picking ↔ raster handshake.
enum class CardinalIndex : int {
    k0 = 0,   ///< rasterYaw ≈ 0      (identity)
    k90 = 1,  ///< rasterYaw ≈ π/2
    k180 = 2, ///< rasterYaw ≈ π
    k270 = 3, ///< rasterYaw ≈ 3π/2
};

/// CPU mirror of `rasterYawCardinalIndex` in `shaders/ir_iso_common.glsl`.
/// `IRPrefab::Camera::computeYawSplit` snaps `rasterYaw` to a multiple of
/// pi/2; the round() defends against bit-wise drift only. Negative inputs
/// fold via `(q mod 4 + 4) mod 4` into [0, 3]. CPU and GPU MUST resolve
/// the same cardinal index for any picking ↔ raster handshake.
constexpr CardinalIndex rasterYawCardinalIndex(float rasterYaw) {
    const int q = static_cast<int>(glm::round(rasterYaw / kHalfPi));
    return static_cast<CardinalIndex>(((q % 4) + 4) % 4);
}

/// `(cos, sin)` of the cardinal angle named by @p cardinalIndex — exact
/// `±1`/`0`, the snapped Z-yaw the `GRID` voxel/SDF rasterizer projects at.
/// Pairs with @ref rasterYawCardinalIndex to retire the open-coded
/// `kCardinalCos`/`kCardinalSin` tables callers used to inline.
///
/// GPU mirror: `cardinalYawCosSin` in `shaders/ir_iso_common.glsl`.
constexpr vec2 cardinalYawCosSin(CardinalIndex cardinalIndex) {
    if (cardinalIndex == CardinalIndex::k90)
        return vec2(0.0f, 1.0f);
    if (cardinalIndex == CardinalIndex::k180)
        return vec2(-1.0f, 0.0f);
    if (cardinalIndex == CardinalIndex::k270)
        return vec2(0.0f, -1.0f);
    return vec2(1.0f, 0.0f);
}

/// CPU mirror of `rotateCardinalZ` in `shaders/ir_iso_common.glsl`.
/// World→view = R_z(-rasterYaw) for the given cardinal snap. The voxel
/// rasterizer applies this to world positions before iso projection, so
/// any screen↔world picking inverse must compose its inverse on the
/// recovered 3D position.
constexpr ivec3 rotateCardinalZ(const ivec3 v, CardinalIndex cardinalIndex) {
    if (cardinalIndex == CardinalIndex::k90)
        return ivec3(v.y, -v.x, v.z);
    if (cardinalIndex == CardinalIndex::k180)
        return ivec3(-v.x, -v.y, v.z);
    if (cardinalIndex == CardinalIndex::k270)
        return ivec3(-v.y, v.x, v.z);
    return v;
}

/// @overload Float variant for forward-projecting non-integer world
/// positions (`C_WorldTransform.translation_` with the modifier-driven
/// offset already folded in) into the rasterYaw-rotated canvas frame. Same
/// R_z(-rasterYaw) sign convention as the integer overload.
constexpr vec3 rotateCardinalZ(const vec3 v, CardinalIndex cardinalIndex) {
    if (cardinalIndex == CardinalIndex::k90)
        return vec3(v.y, -v.x, v.z);
    if (cardinalIndex == CardinalIndex::k180)
        return vec3(-v.x, -v.y, v.z);
    if (cardinalIndex == CardinalIndex::k270)
        return vec3(-v.y, v.x, v.z);
    return v;
}

/// Visible-face triplet (`docs/design/voxel-face-rasterization.md`) for a
/// Z-yaw-only camera (`GRID` mode) snapped to @p cardinalIndex. Returns
/// the three world-frame FaceIds whose outward normal dots positive with
/// the rotated view direction.
///
/// Derivation: view direction in world frame =
/// `rotateCardinalZInv(viewDir, cardinalIndex)` where the unrotated
/// view direction is `(-1,-1,-1)` (camera at +∞ along (1,1,1) looking
/// toward origin). The Z-component never flips for Z-yaw, so the Z slot
/// is always `Z_NEG`. The X/Y axes rotate through the cardinal
/// permutation; slots 0/1 carry the camera-visible polarity per cardinal.
///
/// Slot ordering matches the 2×3 voxel-rasterizer workgroup convention
/// in `localIDToFace_2x3`: slot 0 = X-axis face (the "right column" of
/// the diamond at cardinal 0), slot 1 = Y-axis face (left column), slot
/// 2 = Z-axis face (top row). Per-cardinal slot ↔ FaceId mapping:
///
/// - cardinal 0:   {X_NEG, Y_NEG, Z_NEG}
/// - cardinal 90:  {Y_NEG, X_POS, Z_NEG}
/// - cardinal 180: {X_POS, Y_POS, Z_NEG}
/// - cardinal 270: {Y_POS, X_NEG, Z_NEG}
///
/// At cardinal 0 the result reduces to the pre-#1278 three-face set the
/// rasterizer always assumed; at every other cardinal the rasterizer
/// now picks the actually-visible faces instead of the back-facing ones,
/// fixing the stripe artifact in #1256.
///
/// DETACHED-canvas paths call @ref visibleTriplet (#1386) instead — per-entity
/// rotation determines which faces the octahedral-snap residual acts on, so
/// the canvas SO(3) bake (#1075/#1076) alone is not sufficient.
constexpr std::array<FaceId, 3> visibleFaceTripletCardinal(CardinalIndex cardinalIndex) {
    switch (cardinalIndex) {
    case CardinalIndex::k90:
        return {FaceId::Y_NEG, FaceId::X_POS, FaceId::Z_NEG};
    case CardinalIndex::k180:
        return {FaceId::X_POS, FaceId::Y_POS, FaceId::Z_NEG};
    case CardinalIndex::k270:
        return {FaceId::Y_POS, FaceId::X_NEG, FaceId::Z_NEG};
    case CardinalIndex::k0:
    default:
        return {FaceId::X_NEG, FaceId::Y_NEG, FaceId::Z_NEG};
    }
}

/// Model-frame iso depth axis for a detached entity rotated by @p rotation:
/// `R⁻¹ · (1,1,1)`. The iso projection's depth axis is the world (1,1,1)
/// direction (@ref pos3DtoDistance, `depth = x+y+z`); a detached canvas
/// rasters its voxels in model space (camera yaw zeroed), so the per-voxel
/// metric that orders them front-to-back under the *rotated* view must
/// project onto this model-frame axis rather than the fixed (1,1,1).
/// Symmetric with @ref visibleTriplet's `R⁻¹·viewDir` face selection — one
/// per-entity frame drives both which faces show and how they occlude. Using
/// the fixed (1,1,1) instead is correct only when `R` fixes (1,1,1); off that
/// the snapped order leaks back-face voxels through (the "pitch/roll reveals"
/// bug, live since #1386/#1398 landed full-SO(3) face selection).
///
/// At identity the axis is exactly (1,1,1) — `rotateVectorByQuat` by the
/// identity quat is the exact identity — so @ref isoDepthAlongAxis collapses
/// to `x+y+z` and the detached path stays byte-identical to master; the GRID
/// world canvas keeps the fixed (1,1,1) and is untouched.
///
/// CPU-computed and uploaded into `FrameDataVoxelToCanvas::voxelDepthAxis_`;
/// the shader does only the `roundHalfUp(dot(pos, axis))` projection (GPU
/// mirror `isoDepthAlongAxis` in ir_iso_common.glsl/.metal). Reused by the
/// detached SO(3) forward-scatter composite (#1462 P1 → #1464 P3).
inline vec3 isoDepthAxisModel(const vec4 &rotation) {
    return rotateVectorByQuat(vec3(1.0f, 1.0f, 1.0f), quatInverse(rotation));
}

/// Per-voxel iso occlusion depth: model position @p pos projected onto a
/// (possibly entity-rotated) iso depth @p axis. The SO(3) generalization of
/// @ref pos3DtoDistance — identical to it when `axis == (1,1,1)`. Rounds via
/// @ref roundHalfUp so it matches the GPU mirror (`isoDepthAlongAxis` in
/// ir_iso_common.glsl/.metal) bit-for-bit at half-integer projections.
inline Distance isoDepthAlongAxis(const ivec3 pos, const vec3 axis) {
    return roundHalfUp(dot(vec3(pos), axis));
}

/// The three camera-visible cube faces for an entity rotated by @p rotation —
/// the SO(3) generalization of @ref visibleFaceTripletCardinal (which only
/// covers the camera's Z-yaw cardinals). A cube face is visible when its
/// world-space outward normal opposes the iso view direction: the camera
/// looks down the +(1,1,1) iso depth axis (see engine/math/CLAUDE.md,
/// `depth = x + y + z`, higher = further), so model normal `n` is front-facing
/// iff `(R·n)·viewDir < 0`. Expressed in the entity's model frame that is
/// `n·(R⁻¹·viewDir) < 0`, so the per-axis sign of `R⁻¹·viewDir` picks the
/// POS or NEG face of each axis.
///
/// Slot ordering matches @ref visibleFaceTripletCardinal and the rasterizer's
/// `faceDeform_[]` upload: slot 0 = X-axis face, 1 = Y-axis face, 2 = Z-axis
/// face. Each slot is axis-fixed, so the per-slot `faceDeformationMatrixSO3`
/// (axis-only) is unchanged by this — only which polarity each slot renders.
///
/// At identity rotation `R⁻¹·viewDir = (1,1,1)` (all positive) so the result is
/// `{X_NEG, Y_NEG, Z_NEG}` — byte-identical to `visibleFaceTripletCardinal(k0)`
/// and to the legacy hardcoded detached triplet. Within an octahedral cell the
/// residual (@ref octahedralSnapResidual) stays below the covering radius, so a
/// face only flips polarity when it is edge-on (its view-space normal ≈ 0) —
/// where the deformation is near-singular and the face shows no visible area,
/// making the choice visually inconsequential.
///
/// CPU-only by design: the result is uploaded into
/// `FrameDataVoxelToCanvas::visibleFaceIds_` and consumed shader-side exactly
/// like the cardinal path (#1278), so there is no GPU-side mirror to keep in
/// sync. Reused verbatim by per-entity main-canvas SO(3) (#1299).
inline std::array<FaceId, 3> visibleTriplet(const vec4 &rotation) {
    // View direction expressed in the entity's model frame (R⁻¹ · viewDir) —
    // the same axis the per-voxel occlusion depth projects onto (@ref
    // isoDepthAxisModel). Only the per-axis signs matter here.
    const vec3 viewInModel = isoDepthAxisModel(rotation);
    return {
        viewInModel.x < 0.0f ? FaceId::X_POS : FaceId::X_NEG,
        viewInModel.y < 0.0f ? FaceId::Y_POS : FaceId::Y_NEG,
        viewInModel.z < 0.0f ? FaceId::Z_POS : FaceId::Z_NEG,
    };
}

/// CPU mirror of `cardinalLowerCornerShift` in `shaders/ir_iso_common.glsl`.
/// After `rotateCardinalZ`, the unit voxel's view-space AABB lower corner
/// is offset from the rotated origin because R_z permutes/negates axes.
/// Adding this shift to the rotated voxel position keeps the rasterizer's
/// diamond 2x3 emit aligned with the voxel's iso footprint at every
/// cardinal. Zero at cardinal 0 so the cardinal-snap path is unchanged.
constexpr ivec3 cardinalLowerCornerShift(CardinalIndex cardinalIndex) {
    if (cardinalIndex == CardinalIndex::k90)
        return ivec3(0, -1, 0);
    if (cardinalIndex == CardinalIndex::k180)
        return ivec3(-1, -1, 0);
    if (cardinalIndex == CardinalIndex::k270)
        return ivec3(-1, 0, 0);
    return ivec3(0, 0, 0);
}

/// CPU mirror of `rotateCardinalZInv` in `shaders/ir_iso_common.glsl`.
/// View→world = R_z(+rasterYaw). Use after `isoPixelToPos3D` to lift a
/// reconstructed 3D position back to true world coordinates.
constexpr vec3 rotateCardinalZInv(const vec3 v, CardinalIndex cardinalIndex) {
    if (cardinalIndex == CardinalIndex::k90)
        return vec3(-v.y, v.x, v.z);
    if (cardinalIndex == CardinalIndex::k180)
        return vec3(-v.x, -v.y, v.z);
    if (cardinalIndex == CardinalIndex::k270)
        return vec3(v.y, -v.x, v.z);
    return v;
}

/// Integer view→world variant matching GLSL `rotateCardinalZInvI`.
constexpr ivec3 rotateCardinalZInvI(const ivec3 v, CardinalIndex cardinalIndex) {
    if (cardinalIndex == CardinalIndex::k90)
        return ivec3(-v.y, v.x, v.z);
    if (cardinalIndex == CardinalIndex::k180)
        return ivec3(-v.x, -v.y, v.z);
    if (cardinalIndex == CardinalIndex::k270)
        return ivec3(v.y, -v.x, v.z);
    return v;
}

/// Iso projection of a world point under a continuous Z-yaw camera.
///
/// Equivalent to `pos3DtoPos2DIso(R_z(-yaw) · world)` — applies the world→view
/// rotation for camera yaw @p visualYaw, then projects to iso. Sign convention
/// matches `rotateCardinalZ` (world→view = R_z(-yaw)) so this is the smooth
/// extension of the cardinal-snap projection used by the voxel rasterizer.
///
/// Use cases: CPU iso-bounds computation under continuous yaw, picking
/// pre-image, debug overlay placement. The voxel/SDF rasterizer itself
/// emits at the cardinal `rasterYaw` and applies `faceDeformationMatrix` for
/// the leftover `residualYaw`; this helper is the cleaner CPU-side path
/// when the caller has the full `visualYaw` and doesn't need the split.
///
/// GPU mirror: `pos3DtoPos2DIsoYawed` in `shaders/ir_iso_common.glsl`.
constexpr vec2 pos3DtoPos2DIsoYawed(const vec3 worldPos, float visualYaw) {
    const float c = glm::cos(visualYaw);
    const float s = glm::sin(visualYaw);
    const float vx = worldPos.x * c + worldPos.y * s;
    const float vy = -worldPos.x * s + worldPos.y * c;
    return vec2(-vx + vy, -vx - vy + 2.0f * worldPos.z);
}

/// Conservative XY growth of an axis-aligned half-extent swept under a Z-yaw of
/// `(cosYaw, sinYaw)`: each in-plane axis grows to `|c|·hX + |s|·hY` (the
/// footprint the rotated box covers, up to the √2 extent at ±45°); Z is
/// unchanged. Centralizes the iso-cull / GPU-tile-dispatch footprint expansion
/// the SDF + voxel paths used to inline at each call site, so the CPU cull and
/// the GPU rasterizer grow their bounds identically.
///
/// GPU mirror: `yawGrownIsoHalfExtent` in `shaders/ir_iso_common.glsl`.
constexpr vec3 yawGrownIsoHalfExtent(const vec3 halfExtent, float cosYaw, float sinYaw) {
    const float absC = abs(cosYaw);
    const float absS = abs(sinYaw);
    return vec3(
        halfExtent.x * absC + halfExtent.y * absS,
        halfExtent.x * absS + halfExtent.y * absC,
        halfExtent.z
    );
}

/// 2x2 deformation matrix that maps a face's un-yawed iso-pixel offset to the
/// offset under residual yaw @p residualYaw (in `[-π/4, π/4]`).
///
/// Geometric construction: each face contributes one "u" tangent (in-plane,
/// rotates with the world's Z-yaw) and one "v" tangent (along world Z, fixed
/// under Z-yaw). The original iso-projection columns `M_0` map (u, v) → iso;
/// rotating the world by `R_z(-φ)` produces new columns `M_φ`. The returned
/// `mat2 D_φ = M_φ · M_0⁻¹` post-multiplies an iso-pixel offset emitted at
/// the cardinal `rasterYaw` to recover its position at the continuous yaw.
///
/// Closed form per face (with `c = cos(φ)`, `s = sin(φ)`):
/// - X face: `[c-s, 0; 1-(c+s), 1]`
/// - Y face: `[c+s, 0; c-s-1, 1]`
/// - Z face: `[c, s; -s, c]` = `R_z(-φ)` in 2D iso space
///
/// At `residualYaw == 0` all three are identity, so the cardinal-snap path
/// is bit-identical to the un-yawed projection. `@p face` uses the shader
/// convention `kXFace`/`kYFace`/`kZFace` (0/1/2); other values return identity.
///
/// GPU mirror: `faceDeformationMatrix` in `shaders/ir_iso_common.glsl`.
constexpr mat2 faceDeformationMatrix(int face, float residualYaw) {
    const float c = glm::cos(residualYaw);
    const float s = glm::sin(residualYaw);
    if (face == kXFace) {
        return mat2(c - s, 1.0f - (c + s), 0.0f, 1.0f);
    }
    if (face == kYFace) {
        return mat2(c + s, c - s - 1.0f, 0.0f, 1.0f);
    }
    if (face == kZFace) {
        return mat2(c, -s, s, c);
    }
    return mat2(1.0f, 0.0f, 0.0f, 1.0f);
}

/// FaceId-accepting overload of @ref faceDeformationMatrix. The deformation
/// matrix is axis-only (X_NEG and X_POS share the X-face matrix, etc.) — a
/// face's iso footprint shape depends on its tangent plane, which is shared
/// across the two opposite faces of the axis. Used by the per-slot
/// `FrameDataVoxelToCanvas::faceDeform_[]` upload in
/// `system_voxel_to_trixel::buildVoxelFrameData`.
constexpr mat2 faceDeformationMatrix(FaceId face, float residualYaw) {
    return faceDeformationMatrix(faceAxis(face), residualYaw);
}

/// SO(3) generalization of @ref faceDeformationMatrix: the 2x2 matrix that
/// maps a face's un-rotated iso-pixel offset to its offset after the entity
/// is rotated by the quaternion @p rotationQuat — any axis, not just world Z.
///
/// Each face spans two world tangents (u, v). The un-rotated iso columns
/// `M_0(face) = [iso(u) | iso(v)]` are constant per face; rotating the entity
/// gives `M_R(face) = [iso(R·u) | iso(R·v)]`, and the returned
/// `D = M_R · M_0⁻¹` post-multiplies an iso-pixel offset. At identity rotation
/// `D` is the identity. A pure-Z quaternion reduces to
/// `faceDeformationMatrix(face, -yaw)` — the camera-residual sign convention
/// is opposite the entity-rotation sign.
///
/// A face rotated edge-on projects both tangents parallel, so `D` becomes
/// singular — it flattens that face's offsets onto a line, which is the
/// correct projection of a face seen edge-on. `D` is always finite (it is
/// applied directly to offsets, never inverted), so no degeneracy guard is
/// needed.
///
/// CPU-only by design: T-295's `buildVoxelFrameData` bakes the result into the
/// `faceDeform_` UBO that the voxel-emit shader already applies, so the shader
/// stays rotation-agnostic — there is no GPU-side mirror to keep in sync.
inline mat2 faceDeformationMatrixSO3(int face, const vec4 &rotationQuat) {
    // Per-face world tangents and the constant inverse of the un-rotated iso
    // basis M_0(face). M_0 columns are iso(u), iso(v); the inverses below are
    // those 2x2 matrices inverted (column-major, glm `mat2(c0x,c0y,c1x,c1y)`).
    vec3 u;
    vec3 v;
    mat2 m0Inv;
    if (face == kXFace) {
        u = vec3(0.0f, 1.0f, 0.0f);
        v = vec3(0.0f, 0.0f, 1.0f);
        m0Inv = mat2(1.0f, 0.5f, 0.0f, 0.5f);
    } else if (face == kYFace) {
        u = vec3(1.0f, 0.0f, 0.0f);
        v = vec3(0.0f, 0.0f, 1.0f);
        m0Inv = mat2(-1.0f, -0.5f, 0.0f, 0.5f);
    } else if (face == kZFace) {
        u = vec3(1.0f, 0.0f, 0.0f);
        v = vec3(0.0f, 1.0f, 0.0f);
        m0Inv = mat2(-0.5f, 0.5f, -0.5f, -0.5f);
    } else {
        return mat2(1.0f, 0.0f, 0.0f, 1.0f);
    }
    const vec3 ru = rotateVectorByQuat(u, rotationQuat);
    const vec3 rv = rotateVectorByQuat(v, rotationQuat);
    // iso() linear part: (-x + y, -x - y + 2z) — see engine/math/CLAUDE.md.
    const vec2 colU = vec2(-ru.x + ru.y, -ru.x - ru.y + 2.0f * ru.z);
    const vec2 colV = vec2(-rv.x + rv.y, -rv.x - rv.y + 2.0f * rv.z);
    return mat2(colU, colV) * m0Inv;
}

/// Snap a rotation to the nearest of the 24 cube (chiral octahedral)
/// orientations and return that orientation as a quaternion (one of the 24
/// canonical representatives; either sign hemisphere — `q` and `-q` are the
/// same rotation, so callers that feed the result to a rotation matrix or to
/// `visibleTriplet` are sign-invariant).
///
/// A cube is invariant under all 24 octahedral rotations, so substituting the
/// snapped orientation for the live rotation leaves a cube's silhouette
/// unchanged while quantizing its face-visibility to one of 24 discrete states
/// — the "steps through the 24 octahedral orientations" increment per-entity
/// main-canvas SO(3) wants (#1299, PR-A): the GPU prepass matrix and the
/// per-voxel visible triplet both drive off this one snap, so the rotated
/// geometry and the faces it shows always agree.
inline vec4 octahedralSnap(const vec4 &rotation) {
    constexpr float h = 0.5f;
    constexpr float r = 0.70710678118654752f; // 1 / sqrt(2)
    static const vec4 kOctahedral[24] = {
        vec4(0, 0, 0, 1),                                          // identity
        vec4(1, 0, 0, 0),    vec4(0, 1, 0, 0),  vec4(0, 0, 1, 0),  // 180 face
        vec4(h, h, h, h),    vec4(h, h, -h, h), vec4(h, -h, h, h), // 120 vertex
        vec4(h, -h, -h, h),  vec4(-h, h, h, h), vec4(-h, h, -h, h), vec4(-h, -h, h, h),
        vec4(-h, -h, -h, h), vec4(r, 0, 0, r),  vec4(-r, 0, 0, r),  vec4(0, r, 0, r), // 90 face
        vec4(0, -r, 0, r),   vec4(0, 0, r, r),  vec4(0, 0, -r, r),  vec4(r, r, 0, 0),
        vec4(r, -r, 0, 0),   vec4(r, 0, r, 0), // 180 edge
        vec4(r, 0, -r, 0),   vec4(0, r, r, 0),  vec4(0, r, -r, 0),
    };
    int best = 0;
    float bestDot = -1.0f;
    for (int i = 0; i < 24; ++i) {
        // q and -q are the same rotation, so compare on |dot|.
        const float d = glm::abs(glm::dot(rotation, kOctahedral[i]));
        if (d > bestDot) {
            bestDot = d;
            best = i;
        }
    }
    return kOctahedral[best];
}

/// Snap a rotation to the nearest of the 24 cube (chiral octahedral)
/// orientations and return the residual rotation `R_snap⁻¹ · R`, normalized
/// to the `w >= 0` hemisphere.
///
/// A cube is invariant under all 24 octahedral rotations, so the snap is
/// visually a no-op — but it bounds the residual to the octahedral covering
/// radius. T-295 deforms a detached canvas's voxel emit by this residual
/// rather than the full rotation, keeping `faceDeformationMatrixSO3`'s
/// per-face skew in its clean (small-angle) range so pitch / roll do not
/// degrade into forward-mapped scanline gaps. (Non-cube voxel objects also
/// need `R_snap` applied as a coordinate permutation — a separate step.)
inline vec4 octahedralSnapResidual(const vec4 &rotation) {
    const vec4 s = octahedralSnap(rotation);
    const vec4 sConjugate = vec4(-s.x, -s.y, -s.z, s.w);
    vec4 residual = quatMul(sConjugate, rotation);
    if (residual.w < 0.0f) {
        residual = vec4(-residual.x, -residual.y, -residual.z, -residual.w);
    }
    return residual;
}

/// Residual-yaw-deformed trixel iso-pixel offset within the 2x3 face diamond.
///
/// CPU mirror of `deformedTrixelIsoPixel` in `shaders/ir_iso_common.glsl`:
/// takes the un-yawed offset from `faceOffset_2x3(face, subPixel)`, applies
/// `faceDeformationMatrix(face, residualYaw)`, and rounds back to integer
/// iso pixels via @ref roundHalfUp. `@p subPixel` is 0 or 1 (selects the
/// upper or lower trixel of the face's vertical pair); `@p face` uses the
/// shader convention `kXFace`/`kYFace`/`kZFace`.
constexpr ivec2 deformedTrixelIsoPixel(int face, int subPixel, float residualYaw) {
    ivec2 unyawed;
    if (face == kXFace) {
        unyawed = ivec2(1, 1 + subPixel);
    } else if (face == kZFace) {
        unyawed = ivec2(subPixel, 0);
    } else {
        unyawed = ivec2(
            0,
            1 + subPixel
        ); // kYFace + out-of-range; mirrors faceOffset_2x3 GLSL fallthrough
    }
    const mat2 D = faceDeformationMatrix(face, residualYaw);
    const vec2 deformed = D * vec2(unyawed);
    return ivec2(roundHalfUp(deformed.x), roundHalfUp(deformed.y));
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

/// Rotation matrix that rotates @p matrix by @p angle radians around @p axis. GLM wrapper.
inline mat4 rotate(const mat4 &matrix, float angle, const vec3 &axis) {
    return glm::rotate(matrix, angle, axis);
}

/// Builds the local→world matrix from an SQT triple (scale, quaternion
/// rotation, translation). Composition is `T · R · S`: a local point
/// `p_local` maps to `R · (S · p_local) + t` — the same ordering that
/// `SYSTEM_PROPAGATE_TRANSFORM` applies when composing parent and child
/// transforms.
///
/// Quaternion layout is the engine canon: `vec4(qx, qy, qz, qw)` with `.w`
/// the scalar. Implementation rotates the scaled basis vectors via
/// @ref rotateVectorByQuat so the matrix-form rotation stays bit-identical
/// to the per-vector quaternion-rotate path used elsewhere in the engine.
///
/// GPU mirror: `sqtToMat4` in `shaders/ir_iso_common.glsl`.
inline mat4 sqtToMat4(const vec3 &scaleVec, const vec4 &rotationQuat, const vec3 &translation) {
    const vec3 col0 = rotateVectorByQuat(vec3(scaleVec.x, 0.0f, 0.0f), rotationQuat);
    const vec3 col1 = rotateVectorByQuat(vec3(0.0f, scaleVec.y, 0.0f), rotationQuat);
    const vec3 col2 = rotateVectorByQuat(vec3(0.0f, 0.0f, scaleVec.z), rotationQuat);
    return mat4(vec4(col0, 0.0f), vec4(col1, 0.0f), vec4(col2, 0.0f), vec4(translation, 1.0f));
}

/// Scale–Quaternion–Translation transform value. Mirrors the field layout of
/// `C_LocalTransform` / `C_WorldTransform`, which live in the prefab layer and
/// so can't be named from `engine/math/`; this is the math-layer value type
/// those components and the skeletal-skinning helpers (`IRPrefab::Skeleton`)
/// share. Quaternion layout is the engine canon `vec4(qx, qy, qz, qw)`,
/// identity `(0, 0, 0, 1)`.
struct SQT {
    vec3 scale_{1.0f, 1.0f, 1.0f};
    vec4 rotation_{0.0f, 0.0f, 0.0f, 1.0f};
    vec3 translation_{0.0f, 0.0f, 0.0f};
};

/// SQT overload of @ref sqtToMat4 — same `T · R · S` ordering.
inline mat4 sqtToMat4(const SQT &transform) {
    return sqtToMat4(transform.scale_, transform.rotation_, transform.translation_);
}

/// Composes `parent ∘ child`, reproducing `SYSTEM_PROPAGATE_TRANSFORM`'s
/// modifier-free composition so a chain folded here matches the
/// `C_WorldTransform` the propagation system writes for the same locals:
///
///     scale       = parent.scale * child.scale
///     rotation    = quatMul(parent.rotation, child.rotation)
///     translation = parent.translation
///                 + rotateVectorByQuat(parent.scale * child.translation,
///                                      parent.rotation)
///
/// Matrix-exact (`sqtToMat4(compose) == sqtToMat4(parent) * sqtToMat4(child)`)
/// for uniform scale; non-uniform scale isn't closed under TRS composition
/// (the exact product needs shear). Joints and bind poses are rigid, so the
/// rig path is exact.
inline SQT sqtCompose(const SQT &parent, const SQT &child) {
    SQT out;
    out.scale_ = parent.scale_ * child.scale_;
    out.rotation_ = quatMul(parent.rotation_, child.rotation_);
    out.translation_ = parent.translation_ +
                       rotateVectorByQuat(parent.scale_ * child.translation_, parent.rotation_);
    return out;
}

/// Analytic inverse: the SQT whose matrix is `inverse(sqtToMat4(t))`. Exact
/// for rigid / uniform-scale transforms — the skinning path inverts the
/// constant bind SQT here rather than numerically inverting its 4×4, which
/// keeps the result clean (the rotation inverse is the conjugate, not a
/// general matrix solve). Non-uniform scale is not representable as a single
/// inverse SQT; the rotation reorders relative to the per-axis scale. Pass
/// uniform (or unit) scale, which every bind pose satisfies.
inline SQT sqtInverse(const SQT &transform) {
    SQT inv;
    inv.scale_ =
        vec3(1.0f / transform.scale_.x, 1.0f / transform.scale_.y, 1.0f / transform.scale_.z);
    inv.rotation_ = quatInverse(transform.rotation_);
    inv.translation_ = inv.scale_ * rotateVectorByQuat(-transform.translation_, inv.rotation_);
    return inv;
}

/// Applies an SRT (or any affine) matrix @p transform to an integer voxel
/// grid cell @p cell, returning the destination integer cell with half-up
/// rounding. Use when GRID-mode rotation needs to re-rasterize a set of
/// authored voxels into world-grid cells under a parent or local transform.
///
/// Affine matrices keep `w == 1`, so the perspective divide is omitted. The
/// round goes through @ref roundHalfUp so CPU and GPU agree on the destination
/// cell at half-integer positions.
///
/// GPU mirror: `matrixApplyToVoxelGrid` in `shaders/ir_iso_common.glsl`.
inline ivec3 matrixApplyToVoxelGrid(const mat4 &transform, const ivec3 &cell) {
    const vec4 worldPos = transform * vec4(vec3(cell), 1.0f);
    return roundVec3HalfUp(vec3(worldPos));
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
/// Iso Y increases upward, and the canvas-to-screen mapping NEGATES iso X
/// (`screen.x = -iso.x`, see pos3DtoPos2DScreen) — increasing iso.x is
/// screen-LEFT, so never derive on-screen X direction from these equations
/// alone.  Both screen adjustments are applied outside this helper.
/// **Never inline these equations** — always use this helper so there is
/// one place to fix coordinate-system bugs.
constexpr ivec2 pos3DtoPos2DIso(const ivec3 position) {
    return ivec2(-position.x + position.y, -position.x - position.y + (2 * position.z));
}

/// @overload vec3 variant returning a floating-point iso position.
constexpr vec2 pos3DtoPos2DIso(const vec3 position) {
    return vec2(-position.x + position.y, -position.x - position.y + (2 * position.z));
}

/// Camera iso offset that pins a world @p focusWorld at a constant on-screen
/// position as the camera Z-yaws by @p visualYaw — the `CAMERA_CENTER`
/// rotation pivot. Producers place content on the canvas at `iso_canvas(W) =
/// pos3DtoPos2DIsoYawed(W, yaw)` and the composite ADDS this offset, so a
/// point's screen position is `iso_canvas(W) + offset`. This offset cancels the
/// focus's yaw-induced canvas drift so the focus holds still and the scene
/// rotates about it:
/// @code
///   screen(F) = pos3DtoPos2DIsoYawed(F, yaw) + cameraYawPivotOffset(...)
///             = cameraIso + pos3DtoPos2DIso(F)      // independent of yaw
/// @endcode
/// i.e. `F` keeps a constant screen position across the full yaw sweep
/// (rotation in place). See `IRRender::getEffectiveCameraIso` for the
/// screen-center focus value, or pass an explicit point of interest (#1921)
/// to rotate about it at its true depth.
///
/// At `visualYaw == 0` the yawed and un-yawed projections coincide, so this
/// returns @p cameraIso exactly — the no-rotate fast path stays byte-identical
/// to `ORIGIN` mode. This is the single source of truth for the CAMERA_CENTER
/// pivot offset (both the screen-center default and the #1921 focus path);
/// never inline the drift-cancel formula.
constexpr vec2
cameraYawPivotOffset(const vec2 cameraIso, const vec3 focusWorld, const float visualYaw) {
    return cameraIso - pos3DtoPos2DIsoYawed(focusWorld, visualYaw) + pos3DtoPos2DIso(focusWorld);
}

/// Returns the `cameraIso` delta that produces an on-screen shift equal to
/// @p isoDelta when the camera is at `RotationPivotMode::CAMERA_CENTER`.
///
/// In `CAMERA_CENTER` mode, applying a delta `Δ` to `cameraIso` shifts
/// on-screen content by `P(R_z(−yaw)·Pinv(Δ,0))` (where `P` is the iso
/// projection and `Pinv` is `isoPixelToPos3D`).  This helper solves the
/// resulting 2×2 linear system for `Δ` such that that expression equals
/// @p isoDelta — so dragging the camera moves content parallel to the
/// drag direction on screen at every yaw.
///
/// **Precondition — `CAMERA_CENTER` only.**  In `ORIGIN` mode
/// `getEffectiveCameraIso()` returns `cameraIso` directly, so a delta
/// `Δ = isoDelta` already produces the correct on-screen shift; the pan
/// systems must use `isoDelta` unchanged (or pass `visualYaw = 0`) in that
/// mode.
///
/// At `visualYaw == 0` returns @p isoDelta exactly (identity).  At
/// `yaw = ±2π/3` the iso projection is geometrically degenerate
/// (`det = 1 + 2·cos(yaw) ≈ 0`); the helper returns @p isoDelta unchanged.
///
/// ```cpp
///   const float panYaw =
///       IRRender::getRotationPivotMode() == IRRender::RotationPivotMode::CAMERA_CENTER
///           ? IRPrefab::Camera::getYaw()
///           : 0.0f;
///   camPos.pos_ = dragStart + cameraMoveRelativeToYaw(deltaIso, panYaw);
/// ```
constexpr vec2 cameraMoveRelativeToYaw(const vec2 isoDelta, const float visualYaw) {
    const float c = cos(visualYaw);
    const float s = sin(visualYaw);
    // det = 1 + 2*cos(yaw); singular at yaw = ±2π/3 (degenerate iso view).
    const float det = 1.0f + 2.0f * c;
    if (det > -1e-6f && det < 1e-6f)
        return isoDelta;
    const float dx = isoDelta.x;
    const float dy = isoDelta.y;
    return vec2(((c + 2.0f) * dx - s * dy) / det, 3.0f * (s * dx + c * dy) / det);
}

/// Projects @p position to screen space by scaling the iso result by
/// @p triangleStepSizeScreen, negating X (`screen.x = -iso.x`: increasing
/// iso.x is screen-left, so world +x is screen-right and world +y
/// screen-left), and applying the backend-specific Y sign from
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
        return point.x >= min_.x && point.x <= max_.x && point.y >= min_.y && point.y <= max_.y;
    }

    /// Returns true if the axis-aligned box spanning [@p aMin, @p aMax]
    /// overlaps these bounds (inclusive on every edge); @p aMin / @p aMax are
    /// the box's min / max iso corners.
    bool overlapsAABB(vec2 aMin, vec2 aMax) const {
        return aMax.x >= min_.x && aMin.x <= max_.x && aMax.y >= min_.y && aMin.y <= max_.y;
    }

    /// Returns the centre of the bounds.
    vec2 center() const {
        return (min_ + max_) * 0.5f;
    }
    /// Returns the size of the bounds (max_ - min_).
    vec2 extent() const {
        return max_ - min_;
    }
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
    vec2 viewCenter = -vec2(canvasOriginOffset) -
                      vec2(glm::floor(cameraIso.x), glm::floor(cameraIso.y)) +
                      vec2(canvasSize) * 0.5f;
    vec2 halfExtent = vec2(canvasSize) / (zoom * 2.0f);
    return {viewCenter - halfExtent - vec2(margin), viewCenter + halfExtent + vec2(margin)};
}

/// Widens an iso-space AABB toward the sun to include off-screen shadow
/// casters. A caster at `visiblePos + sunDir * t` (for `t ∈ [0,
/// sweepDistance]`) casts a shadow onto the on-screen pixel at `visiblePos`,
/// so the swept AABB is the visible AABB expanded by the iso projection of
/// `sunDir * sweepDistance`.
///
/// Used by the iso rasterizers to include off-screen shadow casters: a
/// surface inside the swept AABB but outside the visible AABB doesn't reach
/// the framebuffer, but the screen-space sun-shadow bake projects its trixel
/// distance into the sun-depth map, so the shadow it throws onto an
/// on-screen pixel is still rendered.
///
/// @p sunDir is the direction from the world toward the sun (engine
/// convention: +Z is down). @p sweepDistance is the maximum shadow throw
/// length in voxels; pass 0 (or negative) to return @p visible unchanged.
inline IsoBounds2D shadowFeederIsoBounds(IsoBounds2D visible, vec3 sunDir, float sweepDistance) {
    if (sweepDistance <= 0.0f) {
        return visible;
    }
    const vec2 shift = pos3DtoPos2DIso(sunDir * sweepDistance);
    const vec2 lowExpand = vec2(glm::min(0.0f, shift.x), glm::min(0.0f, shift.y));
    const vec2 highExpand = vec2(glm::max(0.0f, shift.x), glm::max(0.0f, shift.y));
    return {visible.min_ + lowExpand, visible.max_ + highExpand};
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

/// Returns the iso-space AABB of the world-space box [@p worldMin, @p worldMax]
/// projected under a continuous camera Z-yaw of @p visualYaw. Enumerates the 8
/// world corners through @ref pos3DtoPos2DIsoYawed and bounds the result.
///
/// Closed-form O(1) companion to per-voxel cull projection (#1439): a chunk
/// caches its static world-AABB once, and its cull region under the live yaw is
/// recovered by projecting 8 corners instead of re-projecting every voxel each
/// frame. `pos3DtoPos2DIsoYawed` is linear, so the iso-AABB of the projected
/// corners is the tight iso-AABB of the projected box — and for a voxel set
/// that does not fill its world-AABB it is a *conservative superset* of the
/// per-voxel iso bounds (never tighter), so the chunk-visibility gate keeps
/// over-including rather than dropping on-screen geometry. At @p visualYaw == 0
/// it reduces to the un-yawed iso projection of the box corners.
inline IsoBounds2D isoAABBOfWorldAABBUnderYaw(vec3 worldMin, vec3 worldMax, float visualYaw) {
    vec2 corners[8];
    int idx = 0;
    for (int dx = 0; dx <= 1; ++dx) {
        for (int dy = 0; dy <= 1; ++dy) {
            for (int dz = 0; dz <= 1; ++dz) {
                const vec3 corner = vec3(
                    dx ? worldMax.x : worldMin.x,
                    dy ? worldMax.y : worldMin.y,
                    dz ? worldMax.z : worldMin.z
                );
                corners[idx++] = pos3DtoPos2DIsoYawed(corner, visualYaw);
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

/// Returns the tight iso-space axis-aligned bounding box of a rectangular
/// prism entity at @p worldPos with @p voxelSize dimensions.  The un-yawed,
/// axis-aligned special case of @ref isoAABBOfWorldAABBUnderYaw over the box
/// [worldPos, worldPos + voxelSize] (prefer shapeIsoHalfExtent when a
/// conservative approximation is acceptable).
inline IsoBounds2D entityIsoBounds(vec3 worldPos, ivec3 voxelSize) {
    return isoAABBOfWorldAABBUnderYaw(worldPos, worldPos + vec3(voxelSize), 0.0f);
}

/// Returns true if a rectangular-prism entity's iso bounding box overlaps
/// the trixel canvas.  Converts the world AABB to iso, translates to canvas
/// pixels, and tests against [0, canvasSize).
inline bool isEntityOnScreen(
    vec3 worldPos, ivec3 voxelSize, vec2 cameraIso, ivec2 canvasOffsetZ1, ivec2 canvasSize
) {
    IsoBounds2D bounds = entityIsoBounds(worldPos, voxelSize);
    vec2 canvasMin =
        bounds.min_ + vec2(canvasOffsetZ1) + vec2(glm::floor(cameraIso.x), glm::floor(cameraIso.y));
    vec2 canvasMax =
        bounds.max_ + vec2(canvasOffsetZ1) + vec2(glm::floor(cameraIso.x), glm::floor(cameraIso.y));
    return canvasMax.x >= 0 && canvasMin.x < canvasSize.x && canvasMax.y >= 0 &&
           canvasMin.y < canvasSize.y;
}

/// Converts a screen-centre-relative position to iso space by dividing by
/// @p triangleStepSizeScreen.
constexpr vec2 pos2DScreenToPos2DIso(const vec2 screenPos, const vec2 triangleStepSizeScreen) {
    return screenPos / triangleStepSizeScreen;
}

/// Converts a screen-space delta to an iso-space delta.  The backend-specific
/// Y sign from IRPlatform::kGfx is applied automatically.
constexpr vec2 screenDeltaToIsoDelta(const vec2 screenDelta, const vec2 triangleStepSizeScreen) {
    return screenDelta / triangleStepSizeScreen * vec2(1.0f, IRPlatform::kGfx.screenYDirection_);
}

/// Converts an iso-space delta to a screen-space delta (inverse of
/// screenDeltaToIsoDelta).
constexpr vec2 isoDeltaToScreenDelta(const vec2 isoDelta, const vec2 triangleStepSizeScreen) {
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

/// Continuous-yaw depth scalar: the camera-forward iso distance of a world
/// point under a continuous Z-yaw camera —
/// `pos3DtoDistance(R_z(-visualYaw) · worldPos) = x·(cos-sin) + y·(sin+cos) + z`.
/// The yawed companion to @ref pos3DtoDistance (identical to it at
/// `visualYaw == 0`). Smaller = nearer. Use for composite depth offsets that
/// must co-sort with the smooth-yaw SDF + per-axis-voxel paths — the detached
/// canvas composite folds this in so a world-placed solid sorts against the
/// continuous-yaw floor at every yaw, not just cardinals. Rounds half-up to
/// match the GPU mirror. GPU mirror: `yawedIsoDistance` in
/// shaders/ir_iso_common.glsl / .metal.
constexpr Distance pos3DtoDistanceYawed(const vec3 worldPos, float visualYaw) {
    const float c = glm::cos(visualYaw);
    const float s = glm::sin(visualYaw);
    return roundHalfUp(worldPos.x * (c - s) + worldPos.y * (s + c) + worldPos.z);
}

/// Sun-space projection of a world point (#2083): `.xy` = UV along the
/// (@p uHat, @p vHat) orthonormal basis perpendicular to the sun ray,
/// `.z` = depth along the sun ray (`-sunDir`; larger = farther from the sun).
/// The sun-axis companion to @ref pos3DtoDistance — same dot-product basis,
/// only the projection axis parameterizes. The single source for the sun
/// shadow pipeline: the bake driver's cascade-AABB corners (CPU), the caster
/// bake, and the receiver lookup all project through this one definition so
/// caster depth and receiver lookup cannot drift. GPU mirrors:
/// `sunSpaceProject` in shaders/ir_sun_projection.glsl / .metal.
constexpr vec3
sunSpaceProject(const vec3 pos3D, const vec3 uHat, const vec3 vHat, const vec3 sunDir) {
    return vec3(glm::dot(pos3D, uHat), glm::dot(pos3D, vHat), -glm::dot(pos3D, sunDir));
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

/// Camera sub-pixel decomposition for the trixel-canvas → framebuffer →
/// screen blit chain. See @ref cameraSubPixelOffsets.
struct CameraSubPixelOffsets {
    /// Game-pixel offset for the trixel-canvas-to-framebuffer placement
    /// (the translate term in `TRIXEL_TO_FRAMEBUFFER::calcModelMatrix`).
    ivec2 framebufferGamePxOffset_;

    /// Screen-pixel residual for the framebuffer-to-screen upscale blit
    /// (the translate term in `FRAMEBUFFER_TO_SCREEN::calcModelMatrix`).
    ivec2 screenPxResidual_;
};

/// Decomposes the camera's sub-iso-pixel position into a game-pixel-integer
/// portion (for the framebuffer placement) and a screen-pixel residual (for
/// the upscale blit).
///
/// The two outputs are derived from the SAME `subIsoGamePx` quantity with
/// one IRMath::floor() per term, so the pair can never disagree at game-
/// pixel boundaries — that is the anti-vibration invariant that prevents
/// per-frame +/-1px jitter as a smoothly-moving camera crosses a game-pixel
/// boundary at low game resolutions.
///
/// `IRPlatform::kIsoToScreenSign` is applied internally on both outputs:
/// the game-pixel offset multiplies by sign AFTER floor() (to match the
/// trixel-to-framebuffer convention); the screen residual multiplies by
/// sign INSIDE floor() (so floor() always rounds toward -infinity in the
/// motion direction, keeping the upscale residual monotone with camera
/// motion). Both conventions are preserved as-is from the legacy split
/// across `TRIXEL_TO_FRAMEBUFFER` and `FRAMEBUFFER_TO_SCREEN`; the helper
/// is the single source of truth for both.
constexpr CameraSubPixelOffsets
cameraSubPixelOffsets(const vec2 cameraIso, const vec2 zoomLevel, const ivec2 scaleFactor) {
    const vec2 subIsoGamePx = pos2DIsoToPos2DGameResolution(IRMath::fract(cameraIso), zoomLevel);
    const vec2 gamePxFloor = IRMath::floor(subIsoGamePx);
    const vec2 subGamePxResidual = subIsoGamePx - gamePxFloor;
    const vec2 sign = IRPlatform::kIsoToScreenSign;
    return CameraSubPixelOffsets{
        ivec2(gamePxFloor) * ivec2(sign),
        ivec2(IRMath::floor(subGamePxResidual * sign * vec2(scaleFactor))),
    };
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

/// Perpendicular axis pair for structured bindings; avoids repeating (axis+k)%3 at each call site.
constexpr std::pair<int, int> perpendicularAxes(int axis) {
    return {(axis + 1) % 3, (axis + 2) % 3};
}

/// Invokes @p cb(x, y, z) for every integer grid cell in the inclusive
/// AABB [lo, hi]. Use this to retire hand-written triple-nested loops over voxel volumes.
template <typename Callback> inline void iterateAABB(ivec3 lo, ivec3 hi, Callback cb) {
    for (int z = lo.z; z <= hi.z; ++z)
        for (int y = lo.y; y <= hi.y; ++y)
            for (int x = lo.x; x <= hi.x; ++x)
                cb(x, y, z);
}

/// Invokes @p cb(x, y, z) for every cell where both 2D masks agree.
/// @p maskXZ is indexed as [x + z * sizeXZ.x] (front/XZ silhouette);
/// @p maskYZ is indexed as [y + z * sizeY] (side/YZ silhouette).
/// sizeXZ.y is the shared Z extent; sizeY is the Y extent.
template <typename Callback>
inline void apply3DMaskIntersection(
    const std::vector<bool> &maskXZ,
    ivec2 sizeXZ,
    const std::vector<bool> &maskYZ,
    int sizeY,
    Callback cb
) {
    for (int z = 0; z < sizeXZ.y; ++z)
        for (int y = 0; y < sizeY; ++y)
            for (int x = 0; x < sizeXZ.x; ++x) {
                if (!maskXZ[static_cast<std::size_t>(x + z * sizeXZ.x)])
                    continue;
                if (!maskYZ[static_cast<std::size_t>(y + z * sizeY)])
                    continue;
                cb(x, y, z);
            }
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

/// Worst-case texel dimensions for one per-axis trixel canvas used by the
/// smooth camera Z-yaw path (#1308;
/// docs/design/per-axis-trixel-canvas-rotation.md §"Bounded textures + minimum
/// on-screen trixel size"). The three axis canvases are allocated once at this
/// size and reused — never reallocated per frame — so the cost is bounded and
/// only paid while the camera rotates.
///
/// Per dimension the size is the larger of two bounds:
///   • Footprint — derived from the per-face deformation matrix D_φ at the
///     residual-yaw bound ±π/4. Horizontal: the in-plane stretch gives √2·W
///     (D_φ row-0 column magnitude is √2 there). Vertical: the Y/X-face row-1
///     `(c−s−1, 1)` at ±π/4 gives `(−1, 1)`, so the worst-case AABB height is
///     |−1|·W + 1·H = W + H, which exceeds √2·H for any W > (√2−1)·H. The
///     full stretched column is (√2, −1) with length √3, not √2; √2 is only its
///     horizontal component.
///   • Density — a face going edge-on would otherwise need unbounded trixels
///     along the skinny axis; the minimum on-screen trixel size floors it. The
///     cardinal iso canvas packs 2 framebuffer px per trixel horizontally and 1
///     vertically (see gameResolutionToSize2DIso), so flooring at
///     @p minOnScreenTrixelPx framebuffer px caps extra subdivision at
///     2 / floor horizontally and 1 / floor vertically over the cardinal texel
///     count. Zoom scales trixels at TRIXEL_TO_FRAMEBUFFER (not by resizing the
///     canvas), so it does not enter this bound.
inline ivec2 perAxisTrixelCanvasWorstCaseSize(
    const ivec2 cardinalExtent, const float minOnScreenTrixelPx = 1.0f
) {
    const float floorPx = max(minOnScreenTrixelPx, 1.0f);
    const float W = static_cast<float>(cardinalExtent.x);
    const float H = static_cast<float>(cardinalExtent.y);
    // Horizontal: √2 in-plane stretch vs. 2px/trixel density floor.
    const float scaleX = max(kSqrt2, 2.0f / floorPx);
    // Vertical: Y/X-face row-1 shear at ±π/4 gives AABB height W + H, which
    // exceeds the density floor H/floorPx (≤ H ≤ W + H) for any W ≥ 0.
    const float boundsY = max(W + H, H / floorPx);
    return ivec2{static_cast<int>(ceil(W * scaleX)), static_cast<int>(ceil(boundsY))};
}

/// Per-axis lattice-density cap for the smooth-camera-Z-yaw store (#1431).
///
/// The per-axis store (`c_voxel_to_trixel_stage_1`, `perAxisRoute != 0`) writes
/// each voxel face into a per-axis canvas keyed by its un-yawed (cardinal) iso
/// pixel `perAxisBase + pos3DtoPos2DIso(facePos)` (see `ir_iso_common.glsl`
/// `pos3DtoPos2DIso`/`isoPixelToPos3D`). The canvas is
/// sized once to the base-resolution rotated footprint
/// (@ref perAxisTrixelCanvasWorstCaseSize) and does NOT scale with
/// `subPerAxis`, so a large `subPerAxis` (high `voxel_render_subdivisions`, or
/// high zoom — both fold into `effSub`) drives on-screen cells past
/// `canvasSize` and `isInsideCanvas` silently drops them: the #1431 black-hole
/// clip. This caps `subPerAxis` so the worst-case on-screen cell stays inside
/// the canvas:
///
///     subPerAxisCapped = min(effSub, floor( canvasHalf / maxOnScreenWorldDisp ))
///
/// `maxOnScreenWorldDisp` is the largest in-plane world displacement of an
/// on-screen voxel from the camera-tracked anchor: the visible cardinal-iso
/// half-extent (`cardinalExtent / 2·zoom`) inverted to world on the depth-0
/// plane (the `isoPixelToPos3D` closed form: `|wx|,|wy| ≤ isoHalf.x/2 +
/// isoHalf.y/6`, `|wz| ≤ isoHalf.y/3`), grown by the √2 residual-yaw rotation
/// footprint the canvas was sized for (@ref kSqrt2). The cap MUST be computed
/// CPU-side: `effSub = clamp(m_vrs × round(zoom), 1, 16)` conflates subdivision
/// and zoom, so a shader given only `effSub` + `canvasSize` cannot recover the
/// zoom-dependent on-screen extent — the host knows `zoom` and feeds the capped
/// value through `voxelRenderOptions.y` so the store, the framebuffer scatter,
/// and the per-axis AO/lighting recovery all share one consistent world↔cell
/// scale. Pass the same @p minOnScreenTrixelPx used to allocate the canvas
/// (e.g. `kMinOnScreenTrixelSizePx`) so the cap is sized against the actual
/// allocated canvas rather than the default. Returns ≥ 1.
inline int perAxisSubdivisionCap(
    const ivec2 cardinalExtent, const vec2 zoom, const float minOnScreenTrixelPx = 1.0f
) {
    const float W = static_cast<float>(cardinalExtent.x);
    const float H = static_cast<float>(cardinalExtent.y);
    const ivec2 canvasSize = perAxisTrixelCanvasWorstCaseSize(cardinalExtent, minOnScreenTrixelPx);
    // Smaller zoom ⇒ larger visible extent ⇒ tighter cap; clamp away from 0.
    const float isoHalfX = W / (2.0f * max(zoom.x, 1e-3f));
    const float isoHalfY = H / (2.0f * max(zoom.y, 1e-3f));
    // Depth-0 inverse of isoPixelToPos3D over the visible iso rect. The canvas
    // X axis stores an in-plane x/y world axis; the Y axis stores z (X/Y faces)
    // or x/y (Z face) — bound each by the larger candidate.
    // At depth=0 (x+y+z=0): iso.y=-x-y+2z=3z → z=iso.y/3, y=iso.x/2-iso.y/6.
    const float maxInPlaneXY = 0.5f * isoHalfX + isoHalfY / 6.0f;
    const float maxInPlaneZ = isoHalfY / 3.0f;
    const float maxDispX = maxInPlaneXY;
    const float maxDispY = max(maxInPlaneZ, maxInPlaneXY);
    const float capX = (0.5f * static_cast<float>(canvasSize.x)) / (kSqrt2 * maxDispX);
    const float capY = (0.5f * static_cast<float>(canvasSize.y)) / (kSqrt2 * maxDispY);
    return max(static_cast<int>(floor(min(capX, capY))), 1);
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
    int index, float spacing, PlaneIso plane = PlaneIso::XY, float depth = 0.0f
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
